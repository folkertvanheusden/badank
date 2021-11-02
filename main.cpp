// (C) 2021 by Folkert van Heusden <mail@vanheusden.com>
// Released under Apache License v2.0

#include <atomic>
#include <libconfig.h++>
#include <mutex>
#include <optional>
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
#include "str.h"
#include "time.h"

std::mutex game_file_lock;

typedef enum { R_OK, R_ERROR, R_TIMEOUT } run_result_t;

// result, vector-of-sgf-moves
std::tuple<std::optional<std::string>, std::vector<std::string>, run_result_t> play(GtpEngine *const pb, GtpEngine *const pw, const int dim, GtpEngine *const scorer)
{
	std::vector<std::string> sgf;

	scorer->boardsize(dim);
	pb->boardsize(dim);
	pw->boardsize(dim);

	int whitePass = 0, blackPass = 0;

	color_t color = C_BLACK;

	std::optional<std::string> result;

	run_result_t rr = R_OK;

	for(;;) {
		std::string move;

		if (color == C_BLACK) {
			auto rc = pb->genmove(color, { });

			if (rc.has_value() == false) {
				dolog(info, "Black (%s) stopped responding, white (%s) wins", pb->getname().c_str(), pw->getname().c_str());
				result = "W";
				rr = R_ERROR;
				break;
			}

			move = rc.value();

			pw->play(color, move);
		}
		else {
			auto rc = pw->genmove(color, { });

			if (rc.has_value() == false) {
				dolog(info, "White (%s) stopped responding, black (%s) wins", pw->getname().c_str(), pb->getname().c_str());
				result = "B";
				rr = R_ERROR;
				break;
			}

			move = rc.value();

			pb->play(color, move);
		}

		move = str_tolower(move);

		scorer->play(color, move);

		if (move == "pass") {
			if (color == C_BLACK) {
				blackPass += 1;
				sgf.push_back("B[]");
			}
			else {
				whitePass += 1;
				sgf.push_back("W[]");
			}

			if (blackPass == 3 || whitePass == 3)
				break;
		}
		else if (move == "resign") {
			if (color == C_BLACK)
				result = "W+Resign";
			else
				result = "B+Resign";

			break;
		}
		else {
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

		if (color == C_BLACK)
			color = C_WHITE;
		else
			color = C_BLACK;
	}

	if (result.has_value() == false)
		result = scorer->getscore();

	return { result, sgf, rr };
}

typedef struct {
	std::string command, directory, alt_name;
	std::string name;
	Glicko::Rating rating;
} engine_parameters_t;

typedef struct _stats_t_ {
	std::atomic_int ok { 0 }, error { 0 };
	std::atomic_uint64_t ok_took { 0 };

	_stats_t_() {
	}
} stats_t;

void play_game(const std::string & meta_str, engine_parameters_t *const p1, engine_parameters_t *const p2, const engine_parameters_t & ps, const int dim, const std::string & pgn_file, const std::string & sgf_file, stats_t *const s)
{
	GtpEngine *scorer = new GtpEngine(ps.command, ps.directory, "");

	GtpEngine *inst1 = new GtpEngine(p1->command, p1->directory, p1->alt_name);
	std::string name1 = inst1->getname();
	p1->name = name1;

	GtpEngine *inst2 = new GtpEngine(p2->command, p2->directory, p2->alt_name);
	std::string name2 = inst2->getname();
	p2->name = name2;

	dolog(info, "%s%s versus %s started", meta_str.c_str(), name1.c_str(), name2.c_str());

	uint64_t start_ts = get_ts_ms();

	auto resultrc = play(inst1, inst2, dim, scorer);
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

	if (std::get<2>(resultrc) == R_OK) {
		s->ok++;
		s->ok_took += took;
	}
	else if (std::get<2>(resultrc) == R_ERROR) {
		s->error++;
	}

	std::string result_pgn = "1/2-1/2";

	if (result.at(0) == 'b') {
		result_pgn = "0-1";
		p1->rating.Update(p2->rating, 1.0);
		p2->rating.Update(p1->rating, 0.0);
	}
	else if (result.at(0) == 'w') {
		result_pgn = "1-0";
		p1->rating.Update(p2->rating, 0.0);
		p2->rating.Update(p1->rating, 1.0);
	}
	else {
		p1->rating.Update(p2->rating, 0.5);
		p2->rating.Update(p1->rating, 0.5);
	}

	p1->rating.Apply();
	p2->rating.Apply();

	game_file_lock.lock();
	if (pgn_file.empty() == false) {
		FILE *fh = fopen(pgn_file.c_str(), "a+");
		if (fh) {
			fprintf(fh, "[White \"%s\"]\n[Black \"%s\"]\n[Result \"%s\"]\n\n%s\n\n", name2.c_str(), name1.c_str(), result_pgn.c_str(), result_pgn.c_str());
			fclose(fh);
		}
	}

	if (sgf_file.empty() == false) {
		FILE *fh = fopen(sgf_file.c_str(), "a+");
		if (fh) {
			fprintf(fh, "(;PW[%s]\nPB[%s]\nRE[%s]\n(", name2.c_str(), name1.c_str(), str_toupper(result).c_str());

			for(const std::string & vertex : std::get<1>(resultrc))
				fprintf(fh, ";%s", vertex.c_str());

			fprintf(fh, ")\n)\n\n");

			fclose(fh);
		}
	}
	game_file_lock.unlock();

	dolog(info, "%s (black; %f elo) versus %s (white; %f elo) result: %s, took: %fs", name1.c_str(), p1->rating.Rating1(), name2.c_str(), p2->rating.Rating1(), result.c_str(), (end_ts - start_ts) / 1000.0);

	delete inst2;

	delete inst1;

	delete scorer;
}

typedef struct {
	engine_parameters_t *p1, *p2;
	int nr;
} work_t;

Queue<work_t> q;

void processing_thread(const engine_parameters_t & scorer, const int dim, const std::string & pgn_file, const std::string & sgf_file, stats_t *const s)
{
	for(;;) {
		work_t entry = q.pop();
		if (entry.p1->command.empty()) {
			dolog(info, "Work finished, terminating thread");
			break;
		}

		std::string meta = myformat("%d> ", entry.nr);

		play_game(meta, entry.p1, entry.p2, scorer, dim, pgn_file, sgf_file, s);
	}
}

void play_batch(const std::vector<engine_parameters_t *> & engines, const engine_parameters_t & scorer, const int dim, const std::string & pgn_file, const std::string & sgf_file, const int concurrency, const int iterations, stats_t *const s)
{
	dolog(info, "Batch starting");

	size_t n = engines.size();

	dolog(info, "Will play %ld games", n * (n - 1) * iterations);

	std::vector<std::thread *> threads;

	for(int i=0; i<concurrency; i++) {
		std::thread *th = new std::thread(processing_thread, scorer, dim, pgn_file, sgf_file, s);
		threads.push_back(th);
	}

	int nr = 0;

	for(int i=0; i<iterations; i++) {
		for(size_t a=0; a<n; a++) {
			for(size_t b=0; b<n; b++) {
				if (a == b)
					continue;

				q.push({engines[a], engines[b], nr});
				nr++;
			}
		}
	}

	engine_parameters_t ep;

	for(int i=0; i<concurrency; i++)
		q.push({&ep, &ep, -1});

    	dolog(info, "Waiting for threads to finish...");

	for(std::thread *th : threads) {
		th->join();
		delete th;
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

int main(int argc, char *argv[])
{
	setlog("badank.log", debug, info);
	dolog(notice, " * Badank started *");

	std::vector<engine_parameters_t *> eo;  // engine objects

	libconfig::Config cfg;
	cfg.readFile("badank.cfg");

	libconfig::Setting & root = cfg.getRoot();

	libconfig::Setting & engines = root.lookup("engines");
	size_t n_engines = engines.getLength();

	for(size_t i=0; i<n_engines; i++) {
		libconfig::Setting &engine_root = engines[i];

		std::string command = (const char *)engine_root.lookup("command");
		std::string dir = (const char *)engine_root.lookup("dir");
		std::string alt_name = (const char *)engine_root.lookup("alt_name");

		engine_parameters_t *p = new engine_parameters_t({ command, dir, alt_name });
		eo.push_back(p);
	}

	std::string scorer_command = (const char *)root.lookup("scorer_command");
	std::string scorer_dir = (const char *)root.lookup("scorer_dir");
	engine_parameters_t scorer { scorer_command, scorer_dir, "" };

	std::string pgn_file = (const char *)root.lookup("pgn_file");

	std::string sgf_file = (const char *)root.lookup("sgf_file");

	int concurrency = cfg.lookup("concurrency");

	int n_games = cfg.lookup("n_games");

	int dim = cfg.lookup("board_size");

	signal(SIGPIPE, SIG_IGN);

	test_config(eo);

	stats_t s;

	uint64_t start_ts = get_ts_ms();
	play_batch(eo, scorer, dim, pgn_file, sgf_file, concurrency, n_games, &s);
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
	dolog(info, "-------");
	for(engine_parameters_t *ep : eo) {
		dolog(info, "%s: %.1f elo", ep->name.c_str(), ep->rating.Rating1());

		delete ep;
	}
	dolog(info, "-------");

	dolog(notice, " * Badank finished *");

	return 0;
}
