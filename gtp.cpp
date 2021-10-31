#include <optional>
#include <string>

#include "color.h"
#include "gtp.h"
#include "proc.h"
#include "str.h"

GtpEngine::GtpEngine(const std::string & program, const std::string & dir)
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
		if (rc.has_value() == false)
			return { };

		if (rc.value().size() >= 1) {
			if (rc.value().at(0) == '=')
				return trim(rc.value().substr(1));
		}
	}
}

std::optional<std::string> GtpEngine::genmove(const color_t c, const std::optional<int> timeout_ms)
{
	std::string cmd = myformat("genmove %c", c == C_WHITE ? 'w' : 'b');

	if (engine->write(cmd))
		return getresponse({ });

	return { };
}

bool GtpEngine::play(const color_t c, const std::string & vertex)
{
	std::string cmd = myformat("play %c %s", c == C_WHITE ? 'w' : 'b', vertex.c_str());

	if (engine->write(cmd))
		return getresponse({ }).has_value();

	return false;
}

bool GtpEngine::boardsize(const int dim)
{
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

std::optional<std::string> GtpEngine::getname()
{
	if (engine->write("name")) {
		auto rc = getresponse({ });

		if (rc.has_value()) {
			name = rc.value();
			return name;
		}
	}

	return { };
}
