// (C) 2021-2023 by Folkert van Heusden <mail@vanheusden.com>
// Released under MIT license

#pragma once
#include <optional>
#include <string>
#include <sys/types.h>

class TextProgram
{
private:
	pid_t pid;
	int r, w;

public:
	TextProgram(const std::string & command, const std::string & dir);
	~TextProgram();

	std::optional<std::string> read(std::optional<int> timeout_ms);

	bool write(const std::string & text);
};
