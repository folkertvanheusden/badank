// (C) 2021-2023 by Folkert van Heusden <mail@vanheusden.com>
// Released under MIT license

#include <optional>
#include <string>

#include "color.h"
#include "gtp.h"
#include "log.h"
#include "proc.h"
#include "str.h"

GtpEngine::GtpEngine(const std::string & program, const std::string & dir, const std::string & alt_name) : program(program), name(alt_name)
{
	engine = new TextProgram(program, dir);
}

GtpEngine::~GtpEngine()
{
	delete engine;
}

std::optional<std::string> GtpEngine::getresponse(const std::optional<int> timeout_ms)
{
	for(;;) {
		auto rc = engine->read(timeout_ms);
		if (rc.has_value() == false) {
			dolog(warning, "Failed reading from %s", name.c_str());
			return { };
		}

		if (rc.value().size() >= 1) {
			dolog(debug, "%s> %s", name.c_str(), rc.value().c_str());

			std::string data = trim(rc.value().substr(1));

			if (rc.value().at(0) == '=')
				return data;

			if (rc.value().at(0) == '?') {
				dolog(warning, "Program %s returned an error: %s", name.c_str(), data.c_str());
				return { };
			}
		}
	}
}

std::optional<std::string> GtpEngine::genmove(const color_t c)
{
	std::string cmd = myformat("genmove %c", c == C_WHITE ? 'w' : 'b');

	dolog(debug, "%s< %s", name.c_str(), cmd.c_str());

	if (engine->write(cmd))
		return getresponse({ });

	return { };
}

bool GtpEngine::play(const color_t c, const std::string & vertex)
{
	std::string cmd = myformat("play %c %s", c == C_WHITE ? 'w' : 'b', vertex.c_str());

	dolog(debug, "%s< %s", name.c_str(), cmd.c_str());

	if (engine->write(cmd))
		return getresponse({ }).has_value();

	return false;
}

bool GtpEngine::setkomi(const double komi)
{
	std::string cmd = myformat("komi %f", komi);

	dolog(debug, "%s< %s", name.c_str(), cmd.c_str());

	if (engine->write(cmd))
		return getresponse({ }).has_value();

	return false;
}

bool GtpEngine::time_settings(const int main_time, const int byo_yomi_time, const int byo_yomi_stones)
{
	std::string cmd = myformat("time_settings %d %d %d", main_time, byo_yomi_time, byo_yomi_stones);

	dolog(debug, "%s< %s", name.c_str(), cmd.c_str());

	if (engine->write(cmd))
		return getresponse({ }).has_value();

	return false;
}

bool GtpEngine::time_left(const color_t c, const int time_left_ms)
{
	std::string cmd = myformat("time_left %c %d 0", c == C_WHITE ? 'w' : 'b', time_left_ms / 1000);

	dolog(debug, "%s< %s", name.c_str(), cmd.c_str());

	if (engine->write(cmd))
		return getresponse({ }).has_value();

	return false;
}

bool GtpEngine::boardsize(const int dim)
{
	dolog(debug, "%s> set board size to %d", name.c_str(), dim);

	if (engine->write(myformat("boardsize %d", dim)))
		return getresponse({ }).has_value();

	return false;
}

bool GtpEngine::clearboard()
{
	if (engine->write("clear_board"))
		return getresponse({ }).has_value();

	return false;
}

std::optional<std::string> GtpEngine::getscore()
{
	if (engine->write("final_score"))
		return getresponse({ });

	return { };
}

std::optional<std::string> GtpEngine::protocol_version()
{
	if (engine->write("protocol_version"))
		return getresponse(30000);  // 30s startup time max.

	return { };
}

std::string GtpEngine::getname()
{
	if (name.empty() && engine->write("name")) {
		auto rc = getresponse({ });

		if (rc.has_value())
			name = rc.value();
		else
			name = program;
	}

	dolog(info, "\"%s\" (%s) plays under PID %d", name.c_str(), program.c_str(), engine->getPid());

	return name;
}
