#include <string>
#include <vector>

#include "color.h"


typedef struct {
	std::vector<std::tuple<color_t, int, int> > moves;
	int    dim;
	double komi;
} book_entry_t;

bool load_sgf_opening_files(const std::string & path, std::vector<book_entry_t> *const entries);
