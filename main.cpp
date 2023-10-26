// (C) 2021-2023 by Folkert van Heusden <mail@vanheusden.com>
// Released under MIT license

#include <algorithm>
#include <atomic>
#include <libconfig.h++>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <thread>
#include <tuple>
#include <sys/resource.h>
#include <sys/time.h>

#include "Glicko2/glicko/rating.hpp"

#include "color.h"
#include "error.h"
#include "gtp.h"
#include "log.h"
#include "queue.h"
#include "sgf.h"
#include "str.h"
#include "time.h"

std::mutex game_file_lock;

std::atomic_bool stop_flag { false };

typedef enum { RR_OK, RR_ERROR, RR_TIMEOUT } run_result_t;

auto produce_seed()
{
	std::vector<unsigned int> random_data(std::mt19937::state_size);

	std::random_device source;
	std::generate(std::begin(random_data), std::end(random_data), [&](){return source();});

	return std::seed_seq(std::begin(random_data), std::end(random_data));
}

thread_local auto mt_seed = produce_seed();
thread_local std::mt19937_64 gen { mt_seed };

bool seed_board_randomly(GtpEngine *const inst1, GtpEngine *const inst2, GtpEngine *const scorer, const int dim, const int n_random_stones, std::vector<std::string> *const sgf)
{
	enum { SR_OK, SR_RETRY, SR_FAIL } seed_result = SR_FAIL;

	const int dimsq  = dim * dim;

	std::vector<std::string> sgf_temp;

        std::uniform_int_distribution<> rng(0, dimsq - 1);

	do {
		bool     *in_use = new bool[dimsq]();

		sgf_temp.clear();

		seed_result = SR_OK;

		for(int i=0; i<n_random_stones * 2; i++) {
			int v = 0;

			do {
				v = rng(gen);
			}
			while(in_use[v]);

			in_use[v] = true;

			color_t c = i & 1 ? C_WHITE : C_BLACK;

			const int x = v % dim;
			const int y = v / dim;

			char x_gtp = 'A' + x;

			if (x_gtp >= 'I')
				x_gtp++;

			std::string vertex = myformat("%c%d", x_gtp, y + 1);

			if (!inst1->play(c, vertex)) {
				seed_result = SR_FAIL;
				break;
			}

			if (!inst2->play(c, vertex)) {
				seed_result = SR_FAIL;
				break;
			}

			// assuming that the scorer is always right
			if (!scorer->play(c, vertex)) {
				seed_result = i > 1 ? SR_RETRY : SR_FAIL;
				break;
			}

			std::string move_str = myformat("%c%c", 'a' + x, 'a' + y);

			if (c == C_BLACK)
				sgf_temp.push_back(myformat("B[%s]", move_str.c_str()));
			else
				sgf_temp.push_back(myformat("W[%s]", move_str.c_str()));
		}

		delete [] in_use;

		if (seed_result == SR_FAIL)
			dolog(warning, "Seeding failed");
		else if (seed_result == SR_RETRY)
			dolog(warning, "Seeding failed - retrying");
	}
	while(seed_result == SR_RETRY);

	if (seed_result == SR_OK) {
		*sgf = sgf_temp;

		return true;
	}

	return false;
}

bool seed_board_from_book(const std::vector<book_entry_t> *const book_entries, GtpEngine *const inst1, GtpEngine *const inst2, GtpEngine *const scorer, std::vector<std::string> *const sgf)
{
	size_t n_entries = book_entries->size();
	size_t entry_idx = rand() % n_entries;

	const auto & book_entry = book_entries->at(entry_idx);

	scorer->boardsize(book_entry.dim);
	inst1->boardsize(book_entry.dim);
	inst2->boardsize(book_entry.dim);

	for(auto & e : book_entry.moves) {
		const color_t c = std::get<0>(e);
		const int     x = std::get<1>(e);
		const int     y = std::get<2>(e);

		char x_gtp = 'A' + x;

		if (x_gtp >= 'I')
			x_gtp++;

		std::string vertex = myformat("%c%d", x_gtp, y + 1);

		if (!inst1->play(c, vertex))
			return false;

		if (!inst2->play(c, vertex))
			return false;

		// assuming that the scorer is always right
		if (!scorer->play(c, vertex))
			return false;

		std::string move_str = myformat("%c%c", 'a' + x, 'a' + y);

		if (c == C_BLACK)
			sgf->push_back(myformat("B[%s]", move_str.c_str()));
		else
			sgf->push_back(myformat("W[%s]", move_str.c_str()));
	}

	return true;
}

typedef struct _stats_t_ {
	std::atomic_int ok    { 0 };
	std::atomic_int error { 0 };
	std::atomic_uint64_t ok_took { 0 };

	std::mutex errors_lock;
	std::map<std::string, int> errors;
	std::map<std::string, std::map<std::string, int> > results;

	_stats_t_() {
	}
} stats_t;

void insert_result(stats_t *const s, const std::string & name, const std::string & key)
{
	std::unique_lock<std::mutex> lck(s->errors_lock);

	if (auto it = s->results.find(name); it == s->results.end()) {
		std::map<std::string, int> entry { std::pair<std::string, int>(key, 1) };

		s->results.insert({ name, entry });
	}
	else {
		auto it2 = it->second.find(key);

		if (it2 == it->second.end())
			it->second.insert(std::pair<std::string, int>(key, 1));
		else
			it2->second++;
	}
}

std::string color_name(const color_t color)
{
	return color == C_BLACK ? "black" : "white";
}

typedef struct {
	double main_time;  // in (fractions of) seconds
	double byo_yomi_time;
	int    byo_yomi_stones;
} time_control_t;

typedef enum { ts_main_time, ts_byo_yomi_time } time_state_t;

// result, vector-of-sgf-moves
std::tuple<std::optional<std::string>, std::vector<std::string>, run_result_t> play(GtpEngine *const pb, GtpEngine *const pw, const int dim, GtpEngine *const scorer, const time_control_t & tc, const int n_random_stones, const std::vector<book_entry_t> *const book_entries, stats_t *const s)
{
	std::vector<std::string> sgf;

	if (pb->clearboard() == false || pw->clearboard() == false || scorer->clearboard() == false) {
		dolog(error, "\"clear_board\" not accepted");

		return { { }, { }, RR_ERROR };
	}

	scorer->boardsize(dim);
	pb->boardsize(dim);
	pw->boardsize(dim);

	GtpEngine *ge[] = { pb, pw };

	bool use_time_left[] = { false, false };

	if (pb->has_command("time_settings"))
		pb->time_settings(tc.main_time, tc.byo_yomi_time, tc.byo_yomi_stones);

	use_time_left[C_BLACK] = pb->has_command("time_left");

	if (pw->has_command("time_settings"))
		pw->time_settings(tc.main_time, tc.byo_yomi_time, tc.byo_yomi_stones);

	use_time_left[C_WHITE] = pw->has_command("time_left");

	if (book_entries->empty() == false) {
		// TODO: get from book
		if (!seed_board_from_book(book_entries, pb, pw, scorer, &sgf)) {
			dolog(error, "Failed to seed board from book for %s versus %s", pb->getname().c_str(), pw->getname().c_str());

			return { { }, { }, RR_ERROR };
		}
	}
	else {
		if (!seed_board_randomly(pb, pw, scorer, dim, n_random_stones, &sgf)) {
			dolog(error, "Failed to seed board randomly for %s versus %s", pb->getname().c_str(), pw->getname().c_str());

			return { { }, { }, RR_ERROR };
		}
	}

	uint64_t time_total[] = { 0, 0 };
	int      n_played[]   = { 0, 0 };

	time_state_t ts[]           = { ts_main_time, ts_main_time };
	int          time_left[]    = { int(tc.main_time * 1000), int(tc.main_time * 1000) };
	int          stones_to_do[] = { 0, 0 };

	color_t  color  = C_BLACK;

	std::optional<std::string> result;

	run_result_t rr = RR_OK;

	insert_result(s, pb->getname(), "black games played");

	insert_result(s, pw->getname(), "white games played");

	bool pass[2] { false, false };

	for(;;) {
		std::string move;

		color_t opponent_color = color == C_BLACK ? C_WHITE : C_BLACK;

		if (ts[color] == ts_main_time)
			dolog(debug, "Player %s has %.3f seconds/, black/white pass: %d/%d", color_name(color).c_str(), time_left[color] / 1000., pass[C_BLACK], pass[C_WHITE]);
		else
			dolog(debug, "Player %s has %.3f seconds/%d stones left, black/white pass: %d/%d", color_name(color).c_str(), time_left[color] / 1000., stones_to_do[color], pass[C_BLACK], pass[C_WHITE]);

		if (use_time_left[color] && ge[color]->time_left(color, time_left[color], ts[color] == ts_main_time ? 0 : stones_to_do[color]) == false) {
			dolog(info, "%s (%s) did not respond to time_left", color_name(color).c_str(), ge[color]->getname().c_str());
			result = "?";
			rr = RR_ERROR;
			break;
		}

		uint64_t start_ts = get_ts_ms();
		auto     rc       = ge[color]->genmove(color);
		uint64_t end_ts   = get_ts_ms();

		if (rc.has_value() == false) {
			dolog(info, "%s (%s) did not return a move (%s)", color_name(color).c_str(), ge[color]->getname().c_str(), ge[color]->get_loghelper().c_str());
			result = "?";
			rr = RR_ERROR;
			break;
		}

		uint64_t took = end_ts - start_ts;
		time_total[color] += took;
		time_left[color] -= took;
		stones_to_do[color]--;

		if (time_left[color] < 0) {
			if (ts[color] == ts_main_time)
				ts[color] = ts_byo_yomi_time;
			else {
				if (stones_to_do[color] != 0) {
					dolog(info, "%s (%s) did not return enought byo yomi moves: %d left", color_name(color).c_str(), ge[color]->getname().c_str(), stones_to_do[color]);
					result = "?";
					rr = RR_ERROR;
					break;
				}
			}

			time_left[color]    = int(tc.byo_yomi_time * 1000);
			stones_to_do[color] = tc.byo_yomi_stones;
		}

		n_played[color]++;

		move = rc.value();

		if (move != "resign")
			ge[opponent_color]->play(color, move);

		move = str_tolower(move);
		
		// TODO: validate move, not only by scorer

		if (move == "resign") {
			if (color == C_BLACK) {
				result = "W+Resign";
				insert_result(s, pb->getname(), "black resign");
			}
			else {
				result = "B+Resign";
				insert_result(s, pw->getname(), "white resign");
			}

			break;
		}
		else if (!scorer->play(color, move)) {
			dolog(warning, "%s (%s) performed an illegal move (move %d, %s)", color_name(color).c_str(), ge[color]->getname().c_str(), n_played[color], ge[color]->get_loghelper().c_str());

			if (color == C_BLACK) {
				result = "W+Illegal";
				insert_result(s, pb->getname(), "black illegal move");
			}
			else {
				result = "B+Illegal";
				insert_result(s, pw->getname(), "white illegal move");
			}

			break;
		}
		else if (time_left[color] < 0) {
			if (color == C_BLACK) {
				result = "W+Time";
				insert_result(s, pb->getname(), "black out of time");
			}
			else {
				result = "B+Time";
				insert_result(s, pw->getname(), "white out of time");
			}

			break;
		}
		else if (move == "pass") {
			bool end = false;

			if (color == C_BLACK) {
				end |= pass[C_BLACK];
				pass[C_BLACK] = true;

				sgf.push_back("B[]");
			}
			else {
				end |= pass[C_WHITE];
				pass[C_WHITE] = true;

				sgf.push_back("W[]");
			}

			if (end)
				break;
		}
		else {
			pass[C_BLACK] = pass[C_WHITE] = false;

			// gtp to sgf
			char column = move.at(0);
			if (column >= 'j')
				column--;

			int row_str = atoi(move.substr(1).c_str());
			char row = 'a' + row_str - 1;

			std::string move_str = myformat("%c%c", column, row);

			if (color == C_BLACK)
				sgf.push_back(myformat("B[%s]", move_str.c_str()));
			else
				sgf.push_back(myformat("W[%s]", move_str.c_str()));
		}

		color = opponent_color;
	}

	dolog(info, "Black (%s) used %.3fs per move (%.3f total), %d moves, white (%s) used %.3fs per move (%.3f total), %d moves",
			pb->getname().c_str(), time_total[C_BLACK] / 1000. / n_played[C_BLACK], time_total[C_BLACK] / 1000., n_played[C_BLACK],
			pw->getname().c_str(), time_total[C_WHITE] / 1000. / n_played[C_WHITE], time_total[C_WHITE] / 1000., n_played[C_WHITE]);

	if (result.has_value() == false)
		result = scorer->getscore();

	if (rr == RR_OK) {
		auto b_result = pb->getscore();
		auto w_result = pw->getscore();

		dolog(info, "Result according to black: %s, according to white: %s, scorer: %s",
				b_result.has_value() ? b_result.value().c_str() : "-",
				w_result.has_value() ? w_result.value().c_str() : "-",
				result.value().c_str());
	}

	return { result, sgf, rr };
}

typedef struct {
	std::string command, directory, alt_name;
	std::string name;
	bool target;

	std::mutex lock;
	Glicko::Rating rating;
} engine_parameters_t;

void play_game(const std::string & meta_str, engine_parameters_t *const p1, engine_parameters_t *const p2, const engine_parameters_t *const ps, const int dim, const std::string & pgn_file, const std::string & sgf_file, stats_t *const s, const time_control_t & tc, const double komi, const int n_random_stones, const std::vector<book_entry_t> *const book_entries)
{
	GtpEngine *scorer = new GtpEngine(ps->command, ps->directory, "");

	GtpEngine *inst1 = new GtpEngine(p1->command, p1->directory, p1->alt_name);
	std::string name1 = inst1->getname();
	p1->name = name1;

	inst1->setkomi(komi);

	GtpEngine *inst2 = new GtpEngine(p2->command, p2->directory, p2->alt_name);
	std::string name2 = inst2->getname();
	p2->name = name2;

	inst2->setkomi(komi);

	dolog(info, "%s%s versus %s started", meta_str.c_str(), name1.c_str(), name2.c_str());

	uint64_t start_ts = get_ts_ms();

	time_t   start_t  = time(nullptr);

	auto resultrc = play(inst1, inst2, dim, scorer, tc, n_random_stones, book_entries, s);
	if (std::get<0>(resultrc).has_value() == false) {
		dolog(info, "Game between %s and %s failed", name1.c_str(), name2.c_str());
		delete inst2;
		delete inst1;
		delete scorer;
		return;
	}

	uint64_t end_ts = get_ts_ms();
	uint64_t took = end_ts - start_ts;

	std::string result = str_tolower(std::get<0>(resultrc).value());

	if (std::get<2>(resultrc) == RR_OK) {
		s->ok++;
		s->ok_took += took;
	}
	else if (std::get<2>(resultrc) == RR_ERROR) {
		s->error++;
	}

	std::string result_pgn = "1/2-1/2";

	double p1_v = 0.0;
	double p2_v = 0.0;

	if (result.at(0) == 'b') {
		result_pgn = "0-1";

		p1_v = 1.0;
		p2_v = 0.0;
	}
	else if (result.at(0) == 'w') {
		result_pgn = "1-0";

		p1_v = 0.0;
		p2_v = 1.0;
	}
	else if (result.at(0) == '?') {
		// some error
		std::string key = name1 + " versus " + name2;

		s->errors_lock.lock();

		auto it = s->errors.find(key);
		if (it == s->errors.end())
			s->errors.insert({ key, 1 });
		else
			it->second++;

		s->errors_lock.unlock();
	}
	else {
		p1_v = 0.5;
		p2_v = 0.5;
	}

	if (result.at(0) != '?') {
		{
			std::unique_lock<std::mutex> lck(p1->lock);
			p1->rating.Update(p2->rating, p1_v);
			p1->rating.Apply();
		}

		{
			std::unique_lock<std::mutex> lck(p2->lock);
			p2->rating.Update(p1->rating, p2_v);
			p2->rating.Apply();
		}
	}

	game_file_lock.lock();

	if (result.at(0) != '?') {
		if (pgn_file.empty() == false) {
			FILE *fh = fopen(pgn_file.c_str(), "a+");
			if (fh) {
				fprintf(fh, "[White \"%s\"]\n[Black \"%s\"]\n[Result \"%s\"]\n\n%s\n\n", name2.c_str(), name1.c_str(), result_pgn.c_str(), result_pgn.c_str());
				fclose(fh);
			}
		}
	}

	if (sgf_file.empty() == false) {
		FILE *fh = fopen(sgf_file.c_str(), "a+");
		if (fh) {
			tm *tm = localtime(&start_t);

			fprintf(fh, "(;AP[Badank]DT[%04d-%02d-%02d]GM[1]KM[%f]SZ[%d]PW[%s]\nPB[%s]\nRE[%s]\nC[%s]RU[Tromp/Taylor]\n(", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, komi, dim, name2.c_str(), name1.c_str(), str_toupper(result).c_str(), meta_str.c_str());

			for(const std::string & vertex : std::get<1>(resultrc))
				fprintf(fh, ";%s", vertex.c_str());

			if (std::get<2>(resultrc) != RR_OK)
				fprintf(fh, ";C[%s]", result.c_str());

			fprintf(fh, ";C[Initial %d black and %d white stones were placed randomly by Badank]", n_random_stones, n_random_stones);

			fprintf(fh, ")\n)\n\n");

			fclose(fh);
		}
	}
	game_file_lock.unlock();

	{
		std::unique_lock<std::mutex> lck1(p1->lock);
		double r1 = p1->rating.Rating1();
		lck1.unlock();

		std::unique_lock<std::mutex> lck2(p2->lock);
		double r2 = p2->rating.Rating2();
		lck2.unlock();

		dolog(info, "%s (black; %f elo) versus %s (white; %f elo) result: %s, took: %fs", name1.c_str(), r1, name2.c_str(), r2, result.c_str(), (end_ts - start_ts) / 1000.0);
	}

	delete inst2;

	delete inst1;

	delete scorer;
}

typedef struct {
	engine_parameters_t *p1, *p2;
	int nr;
} work_t;

void processing_thread(const engine_parameters_t *const scorer, const int dim, const std::string & pgn_file, const std::string & sgf_file, stats_t *const s, std::atomic_bool *const stop_flag, const time_control_t & tc, const double komi, const int n_random_stones, std::vector<book_entry_t> *const book_entries, Queue<work_t> *const q)
{
	for(;!*stop_flag;) {
		work_t entry = q->pop();

		if (entry.p1 == nullptr) {
			dolog(info, "Work finished, terminating thread");
			break;
		}

		std::string meta = myformat("%d> ", entry.nr);

		play_game(meta, entry.p1, entry.p2, scorer, dim, pgn_file, sgf_file, s, tc, komi, n_random_stones, book_entries);
	}
}

void play_batch(const std::vector<engine_parameters_t *> & engines, const engine_parameters_t *const scorer, const int dim, const std::string & pgn_file, const std::string & sgf_file, const int concurrency, const int iterations, stats_t *const s, const time_control_t & tc, const double komi, const int n_random_stones, const std::string & sgf_book_path, std::atomic_bool *const stop_flag)
{
	dolog(info, "Batch starting");

	size_t n = engines.size();

	dolog(info, "Will play %ld games", n * (n - 1) * iterations);

	std::vector<book_entry_t> book_entries;

	if (sgf_book_path.empty() == false)
		load_sgf_opening_files(sgf_book_path, &book_entries);

	Queue<work_t> q;

	std::vector<std::thread *> threads;

	for(int i=0; i<concurrency; i++) {
		std::thread *th = new std::thread(processing_thread, scorer, dim, pgn_file, sgf_file, s, stop_flag, tc, komi, n_random_stones, &book_entries, &q);
		threads.push_back(th);
	}

	std::vector<engine_parameters_t *> targets;

	for(auto & engine : engines) {
		if (engine->target)
			targets.push_back(engine);
	}

	if (targets.empty()) {
		dolog(info, "everybody against everybody");

		int nr = 0;

		for(int i=0; i<iterations; i++) {
			for(size_t a=0; a<n; a++) {
				for(size_t b=0; b<n; b++) {
					if (*stop_flag) {
						dolog(info, "Aborted batching");
						goto abort_batching;
					}

					if (a == b)
						continue;

					q.push({ engines[a], engines[b], nr });
					nr++;
				}
			}
		}
	}
	else {
		dolog(info, "gauntlet(s)");

		int nr = 0;

		for(int i=0; i<iterations; i++) {
			for(auto & target : targets) {
				for(size_t a=0; a<n; a++) {
					if (*stop_flag) {
						dolog(info, "Aborted batching");
						goto abort_batching;
					}

					if (target == engines[a])
						continue;

					if (engines[a] -> target)
						continue;

					q.push({ target, engines[a], nr });
					nr++;

					q.push({ engines[a], target, nr });
					nr++;
				}
			}
		}
	}

abort_batching:
	engine_parameters_t ep;

	for(int i=0; i<concurrency; i++)
		q.push({nullptr, nullptr, -1});

    	dolog(info, "Waiting for threads to finish...");

	while(!threads.empty()) {
		bool joined_any = false;

		for(auto it = threads.begin(); it != threads.end();) {
			if ((*it)->joinable()) {
				(*it)->join();

				delete *it;

				it = threads.erase(it);

				joined_any = true;
			}
			else {
				it++;
			}
		}

		if (joined_any)
			dolog(info, "%zu threads left", threads.size());

		usleep(10000);
	}

    	dolog(info, "Batch finished");
}

void test_config(const std::vector<engine_parameters_t *> & eo)
{
	bool err = false;

	dolog(info, "Verifying configuration...");

	for(auto ep : eo) {
		dolog(info, "Trying %s", ep->command.c_str());

		GtpEngine *test = new GtpEngine(ep->command, ep->directory, ep->alt_name);

		auto rc = test->protocol_version();
		if (rc.has_value() == false) {
			dolog(error, "Cannot talk to: %s", ep->command.c_str());
			err = true;
		}

		delete test;
	}

	if (err) {
		dolog(warning, "Terminating because of error(s)");
		exit(1);
	}
}

void sigh(int sig)
{
	stop_flag = true;

	dolog(notice, "Program termination triggered by ^c (SIGINT)");
}

int main(int argc, char *argv[])
{
	setlog("badank.log", debug, info);

	dolog(notice, " * Badank started *");

	std::string cfg_file = argc == 2 ? argv[1] : "badank.cfg";

	std::vector<engine_parameters_t *> eo;  // engine objects

	try {
		libconfig::Config cfg;

		cfg.readFile(cfg_file.c_str());

		libconfig::Setting & root = cfg.getRoot();

		std::string log_level_screen = (const char *)root.lookup("log_level_screen");
		std::string log_level_file   = (const char *)root.lookup("log_level_file");

		setlog("badank.log", log_level_file, log_level_screen);

		libconfig::Setting & engines = root.lookup("engines");
		size_t n_engines = engines.getLength();

		for(size_t i=0; i<n_engines; i++) {
			libconfig::Setting &engine_root = engines[i];

			engine_parameters_t *ep = new engine_parameters_t();

			ep->command   = (const char *)engine_root.lookup("command");
			ep->directory = (const char *)engine_root.lookup("dir");
			ep->alt_name  = (const char *)engine_root.lookup("alt_name");

			try {
				ep->target = engine_root.lookup("target");
			}
			catch(const libconfig::SettingNotFoundException & e) {
				// not a problem, just not set
				ep->target = false;
			}

			eo.push_back(ep);
		}

		std::string scorer_command = (const char *)root.lookup("scorer_command");
		std::string scorer_dir = (const char *)root.lookup("scorer_dir");
		engine_parameters_t scorer { scorer_command, scorer_dir, "" };

		std::string pgn_file = (const char *)root.lookup("pgn_file");

		std::string sgf_file = (const char *)root.lookup("sgf_file");

		int concurrency = root.lookup("concurrency");

		int n_games = root.lookup("n_games");

		int dim = root.lookup("board_size");

		time_control_t tc { 0 };
		tc.main_time       = root.lookup("main_time");
		tc.byo_yomi_time   = root.lookup("byo_yomi_time");
		tc.byo_yomi_stones = root.lookup("byo_yomi_stones");

		int n_random_stones = root.lookup("n_random_stones");

		double komi = root.lookup("komi");

		std::string sgf_book_path;

		try {
			sgf_book_path = (const char *)root.lookup("sgf_book_path");
		}
		catch(const libconfig::SettingNotFoundException & e) {
			// not a problem, just not set
		}

		signal(SIGPIPE, SIG_IGN);

		test_config(eo);

		signal(SIGINT, sigh);

		stats_t s;

		uint64_t start_ts = get_ts_ms();
		play_batch(eo, &scorer, dim, pgn_file, sgf_file, concurrency, n_games, &s, tc, komi, n_random_stones, sgf_book_path, &stop_flag);
		uint64_t end_ts = get_ts_ms();
		uint64_t took_ts = end_ts - start_ts;

		struct rusage ru;
		if (getrusage(RUSAGE_CHILDREN, &ru) == -1)
			error_exit(true, "getrusage failed");

		uint64_t child_ts = ru.ru_utime.tv_sec * 1000 + ru.ru_utime.tv_usec / 1000;

		dolog(info, "Time used: %fs, cpu factor child processes: %f", took_ts / 1000.0, child_ts / double(took_ts));
		int g_ok = s.ok, g_error = s.error;
		uint64_t g_ok_took = s.ok_took;
		dolog(info, "Games ok: %d (avg duration: %.1fs), games with an error: %d", g_ok, g_ok_took / 1000.0 / g_ok, g_error);

		dolog(info, "ratings:");
		for(engine_parameters_t *ep : eo) {
			dolog(info, "%s: %.1f elo", ep->name.c_str(), ep->rating.Rating1());

			delete ep;
		}
		dolog(info, "-------");

		if (s.errors.empty() == false) {
			dolog(info, "problems:");

			for(auto it: s.errors)
				dolog(info, "%s - %d", it.first.c_str(), it.second);

			dolog(info, "--------");
		}

		dolog(info, "results:");

		for(auto & records: s.results) {
			dolog(info, "%s", records.first.c_str());

			for(auto & record: records.second)
				dolog(info, "  %s: %d", record.first.c_str(), record.second);
		}
	}
	catch(const libconfig::ParseException & pe) {
		error_exit(false, "Error in \"%s\" on line %d: %s", pe.getFile(), pe.getLine(), pe.getError());
	}
	catch(const libconfig::FileIOException & fie) {
		error_exit(false, "Error loading \"%s\"", cfg_file.c_str());
	}

	dolog(notice, " * Badank finished *");

	return 0;
}
