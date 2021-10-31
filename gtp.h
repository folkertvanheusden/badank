#include <optional>
#include <string>

#include "color.h"
#include "proc.h"

class GtpEngine
{
private:
	std::string name;
	TextProgram *engine { nullptr };

	std::optional<std::string> getresponse(const std::optional<int> timeout_ms);

public:
	GtpEngine(const std::string & program, const std::string & dir);
	~GtpEngine();

	std::optional<std::string> genmove(const color_t c, const std::optional<int> timeout_ms);
	bool play(const color_t c, const std::string & vertex);

	bool boardsize(const int dim);
	bool clearboard();

	std::optional<std::string> getscore();

	std::optional<std::string> getname();
};
