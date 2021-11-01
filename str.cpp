// (C) 2021 by Folkert van Heusden <mail@vanheusden.com>
// Released under Apache License v2.0

#include <algorithm>
#include <optional>
#include <stdarg.h>
#include <string>
#include <unistd.h>
#include <vector>

std::string myformat(const char *const fmt, ...)
{
        char *buffer = nullptr;
        va_list ap;

        va_start(ap, fmt);
        (void)vasprintf(&buffer, fmt, ap);
        va_end(ap);

        std::string result = buffer;
        free(buffer);

        return result;
}

std::vector<std::string> split(std::string in, std::string splitter)
{
	std::vector<std::string> out;
	size_t splitter_size = splitter.size();

	for(;;)
	{
		size_t pos = in.find(splitter);
		if (pos == std::string::npos)
			break;

		std::string before = in.substr(0, pos);
		if (!before.empty())
			out.push_back(before);

		size_t bytes_left = in.size() - (pos + splitter_size);
		if (bytes_left == 0)
			return out;

		in = in.substr(pos + splitter_size);
	}

	if (in.size() > 0)
		out.push_back(in);

	return out;
}

std::string str_tolower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });

	return s;
}

std::string merge(const std::vector<std::string> & in, const std::string & seperator)
{
	std::string out;

	for(auto l : in)
		out += l + seperator;

	return out;
}

std::string trim(std::string in, const std::string what)
{
	// right
	in.erase(in.find_last_not_of(what) + 1);

	// left
	in.erase(0, in.find_first_not_of(what));

	return in;
}

std::string replace(std::string target, const std::string & what, const std::string & by_what)
{
        for(;;) {
                std::size_t found = target.find(what);

                if (found == std::string::npos)
                        break;

                std::string before = target.substr(0, found);

                std::size_t after_offset = found + what.size();
                std::string after = target.substr(after_offset);

                target = before + by_what + after;
        }

        return target;
}
