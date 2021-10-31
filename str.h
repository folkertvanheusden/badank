#include <optional>
#include <string>
#include <vector>

std::string myformat(const char *const fmt, ...);

std::vector<std::string> split(std::string in, std::string splitter);
std::string merge(const std::vector<std::string> & in, const std::string & seperator);

std::string str_tolower(std::string s);
std::string trim(std::string in, const std::string what = " ");
std::string replace(std::string target, const std::string & what, const std::string & by_what);
