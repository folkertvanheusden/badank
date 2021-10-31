#include <mutex>
#include <optional>
#include <signal.h>
#include <string>
#include <thread>
#include <sys/resource.h>
#include <sys/time.h>

#include "color.h"
#include "error.h"
#include "gtp.h"
#include "log.h"
#include "queue.h"
#include "str.h"
#include "time.h"

std::mutex pgn_file_lock;

std::optional<std::string> play(GtpEngine *const pb, GtpEngine *const pw, const int dim, GtpEngine *const scorer)
{
	scorer->boardsize(dim);
	pb->boardsize(dim);
	pw->boardsize(dim);

	int whitePass = 0, blackPass = 0;

	color_t color = C_BLACK;

	std::optional<std::string> result;

	for(;;) {
		std::string move;

		if (color == C_BLACK) {
			auto rc = pb->genmove(color, { });

			if (rc.has_value() == false) {
				dolog(info, "Black (%s) stopped responding, white (%s) wins", pb->getname().c_str(), pw->getname().c_str());
				result = "W";
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
				break;
			}

			move = rc.value();

			pb->play(color, move);
		}

		move = str_tolower(move);

		scorer->play(color, move);

		if (move == "pass") {
			if (color == C_BLACK)
				blackPass += 1;
			else
				whitePass += 1;

			if (blackPass == 3 || whitePass == 3)
				break;
		}

		else if (move == "resign") {
			if (color == C_BLACK)
				result = "W";
			else
				result = "B";

			break;
		}

		if (color == C_BLACK)
			color = C_WHITE;
		else
			color = C_BLACK;
	}

	if (result.has_value() == false)
		result = scorer->getscore();

	return result;
}

typedef struct {
	std::string command, directory, alt_name;
} engine_parameters_t;

void play_game(const std::string & meta_str, const engine_parameters_t & p1, const engine_parameters_t & p2, const engine_parameters_t & ps, const int dim, const std::string & pgn_file)
{
	GtpEngine *scorer = new GtpEngine(ps.command, ps.directory, "");

	GtpEngine *inst1 = new GtpEngine(p1.command, p1.directory, p1.alt_name);
	std::string name1 = inst1->getname();

	GtpEngine *inst2 = new GtpEngine(p2.command, p2.directory, p2.alt_name);
	std::string name2 = inst2->getname();

	dolog(info, "%s%s versus %s started", meta_str.c_str(), name1.c_str(), name2.c_str());

	uint64_t start_ts = get_ts_ms();

	auto resultrc = play(inst1, inst2, dim, scorer);
	if (resultrc.has_value() == false) {
		dolog(info, "Game between %s and %s failed", name1.c_str(), name2.c_str());
		delete inst2;
		delete inst1;
		delete scorer;
		return;
	}
	std::string result = str_tolower(resultrc.value());

	std::string result_pgn = "1/2-1/2";

	if (result.at(0) == 'b')
		result_pgn = "0-1";
	else if (result.at(0) == 'w')
		result_pgn = "1-0";

	pgn_file_lock.lock();
	FILE *fh = fopen(pgn_file.c_str(), "a+");
	if (fh) {
		fprintf(fh, "[White \"%s\"]\n[Black \"%s\"]\n[Result \"%s\"]\n\n%s\n\n", name2.c_str(), name1.c_str(), result_pgn.c_str(), result_pgn.c_str());
		fclose(fh);
	}
	pgn_file_lock.unlock();

	uint64_t end_ts = get_ts_ms();

	dolog(info, "%s (black) versus %s (white) result: %s, took: %fs", name1.c_str(), name2.c_str(), result.c_str(), (end_ts - start_ts) / 1000.0);

	delete inst2;

	delete inst1;

	delete scorer;
}

typedef struct {
	engine_parameters_t p1, p2;
	int nr;
} work_t;

Queue<work_t> q;

void processing_thread(const engine_parameters_t & scorer, const int dim, const std::string & pgn_file)
{
	for(;;) {
		work_t entry = q.pop();
		if (entry.p1.command.empty())
			break;

		std::string meta = myformat("%d> ", entry.nr);

		play_game(meta, entry.p1, entry.p2, scorer, dim, pgn_file);
	}
}

void play_batch(const std::vector<engine_parameters_t> & engines, const engine_parameters_t & scorer, const int dim, const std::string & pgn_file, const int concurrency, const int iterations)
{
	dolog(info, "Batch starting");

	size_t n = engines.size();

	dolog(info, "Will play %ld games", n * (n - 1) * iterations);

	std::vector<std::thread *> threads;

	for(int i=0; i<concurrency; i++) {
		std::thread *th = new std::thread(processing_thread, scorer, dim, pgn_file);
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
		q.push({ep, ep, -1});

    	dolog(info, "Waiting for threads to finish...");

	for(std::thread *th : threads) {
		th->join();
		delete th;
	}

    	dolog(info, "Batch finished");
}

int main(int argc, char *argv[])
{
	setlog("badank.log", info, info);

	engine_parameters_t p1 { "/home/folkert/Projects/baduck/build/src/donaldbaduck", "/tmp", "" };
	engine_parameters_t p2 { "/usr/bin/java -jar /home/folkert/Projects/stop/trunk/stop.jar --mode gtp", "/tmp", "" };
	engine_parameters_t p3 { "/home/folkert/Projects/daffyduck/build/src/daffybaduck", "/tmp", "" };
	engine_parameters_t p4 { "/usr/games/gnugo --mode gtp --level 0", "/tmp", "GnuGO level 0" };
	engine_parameters_t p5 { "/home/folkert/amigogtp-1.8/amigogtp/amigogtp", "/tmp", "" };
//	engine_parameters_t p6 { "/home/folkert/Pachi/pachi-12.60-i686 -e pattern -t 1 -D", "/home/folkert/Pachi", "Pachi pattern" };
	engine_parameters_t scorer { "/usr/games/gnugo --mode gtp", "/tmp" };

	std::vector<engine_parameters_t> engines;
	engines.push_back(p1);
	engines.push_back(p2);
	engines.push_back(p3);
	engines.push_back(p4);
	engines.push_back(p5);
//	engines.push_back(p6);

	signal(SIGPIPE, SIG_IGN);

	uint64_t start_ts = get_ts_ms();
	play_batch(engines, scorer, 9, "test2.pgn", 16, 2);
	uint64_t end_ts = get_ts_ms();
	uint64_t took_ts = end_ts - start_ts;

	struct rusage ru;
	if (getrusage(RUSAGE_CHILDREN, &ru) == -1)
		error_exit(true, "getrusage failed");

	uint64_t child_ts = ru.ru_utime.tv_sec * 1000 + ru.ru_utime.tv_usec / 1000;

	dolog(info, "Time used: %fs, cpu factor child processes: %f", took_ts / 1000.0, child_ts / double(took_ts));

	return 0;
}
