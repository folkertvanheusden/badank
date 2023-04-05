// (C) 2021-2023 by Folkert van Heusden <mail@vanheusden.com>
// Released under MIT license

#include <optional>
#include <string>

#include "color.h"
#include "proc.h"

class GtpEngine
{
private:
	const std::string program;
	std::string name;
	TextProgram *engine { nullptr };

	std::optional<std::string> getresponse(const std::optional<int> timeout_ms);

public:
	GtpEngine(const std::string & program, const std::string & dir, const std::string & alt_name);
	~GtpEngine();

	bool setkomi(const double komi);

	std::optional<std::string> genmove(const color_t c);
	bool time_left(const color_t c, const int time_left_ms);
	bool play(const color_t c, const std::string & vertex);

	bool boardsize(const int dim);
	bool clearboard();

	std::optional<std::string> getscore();

	std::optional<std::string> protocol_version();

	std::string getname();
};
