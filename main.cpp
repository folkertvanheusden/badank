#include <optional>
#include <string>

#include "color.h"
#include "gtp.h"
#include "str.h"
#include "time.h"

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
				result = "W";
				break;
			}

			move = rc.value();

			pw->play(color, move);
		}
		else {
			auto rc = pw->genmove(color, { });

			if (rc.has_value() == false) {
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
	std::string command, directory;
} engine_parameters_t;

void play_game(const std::string & meta_str, const engine_parameters_t & p1, const engine_parameters_t & p2, const engine_parameters_t & ps, const int dim, const std::string & pgn_file)
{
	GtpEngine *scorer = new GtpEngine(ps.command, ps.directory);

	GtpEngine *inst1 = new GtpEngine(p1.command, p1.directory);
	auto name1rc = inst1->getname();
	if (name1rc.has_value() == false)
		return;
	std::string name1 = name1rc.value();

	GtpEngine *inst2 = new GtpEngine(p2.command, p2.directory);
	auto name2rc = inst2->getname();
	if (name2rc.has_value() == false)
		return;
	std::string name2 = name2rc.value();

	printf("%s%s versus %s started\n", meta_str.c_str(), name1.c_str(), name2.c_str());

	uint64_t start_ts = get_ts_ms();

	auto resultrc = play(inst1, inst2, dim, scorer);
	if (resultrc.has_value() == false)
		return;
	std::string result = str_tolower(resultrc.value());

	std::string result_pgn = "1/2-1/2";

	if (result.at(0) == 'b')
		result_pgn = "0-1";
	else if (result.at(0) == 'w')
		result_pgn = "1-0";

	FILE *fh = fopen(pgn_file.c_str(), "a+");
	if (fh) {
		fprintf(fh, "[White \"%s\"]\n[Black \"%s\"]\n[Result \"%s\"]\n\n%s\n\n", name2.c_str(), name1.c_str(), result_pgn.c_str(), result_pgn.c_str());
		fclose(fh);
	}

	uint64_t end_ts = get_ts_ms();

	printf("%s (black) versus %s (white) result: %s, took: %fs\n", name1.c_str(), name2.c_str(), result.c_str(), (end_ts - start_ts) / 1000.0);

	delete inst2;

	delete inst1;

	delete scorer;
}

int main(int argc, char *argv[])
{
	engine_parameters_t p1 { "/home/folkert/Projects/baduck/build/src/donaldbaduck", "/tmp" };
	engine_parameters_t p2 { "/usr/bin/java -jar /home/folkert/Projects/stop/trunk/stop.jar --mode gtp", "/tmp" };
	engine_parameters_t scorer { "/usr/games/gnugo --mode gtp", "/tmp" };

	play_game("> ", p1, p2, scorer, 9, "test2.pgn");

	return 0;
}
