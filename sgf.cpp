#include <ctype.h>
#include <dirent.h>
#include <string>
#include <tuple>
#include <vector>

#include "color.h"
#include "error.h"
#include "sgf.h"


// note that this code does not handle multiple roots
bool load_sgf_opening(const std::string & file, book_entry_t *const be)
{
	FILE *fh = fopen(file.c_str(), "r");

	if (!fh)
		error_exit(true, "Cannot open %s", file.c_str());

	be->moves.clear();
	be->komi = 0;
	be->dim  = 19;

	bool        fail    = false;

	bool        get_key = true;
	std::string key;
	std::string value;

	while(!feof(fh) && !fail) {
		int c = fgetc(fh);

		if (c == '(') {
			get_key = true;
			key.clear();
			value.clear();
		}
		else if (c == ';') {
			get_key = true;
			key.clear();
			value.clear();
		}
		else if (get_key == true) {
			if (tolower(c) >= 'a' && tolower(c) <= 'z')
				key += c;
			else if (c == '[')
				get_key = false;
			else {
				get_key = true;
				key.clear();
				value.clear();
			}
		}
		else if (get_key == false) {
			if (c == ']') {
				// process key, value

				if (key == "B" || key == "W") {  // move
					if (value.size() != 2) {
						fail = true;
						break;
					}

					color_t c = key == "B" ? C_BLACK : C_WHITE;

					int     x = toupper(value.at(0)) - 'A';
					int     y = toupper(value.at(1)) - 'A';

					if (x < 0 || y < 0 || x >= be->dim || y >= be->dim) {
						fail = true;
						break;
					}

					be->moves.push_back({ c, x, y });
				}
				else if (key == "SZ") {  // dim
					be->dim = atoi(value.c_str());
				}
				else if (key == "KM") {  // komi
					be->komi = atof(value.c_str());
				}

				get_key = true;
				key.clear();
				value.clear();
			}
			else {
				value += c;
			}
		}
	}

	fclose(fh);

	return !fail;
}

bool load_sgf_opening_files(const std::string & path, std::vector<book_entry_t> *const entries)
{
	DIR *dir = opendir(path.c_str());
	if (!dir)
		error_exit(true, "Cannot open directory %s", path.c_str());

	bool ok = true;

	for(;;) {
		dirent *entry = readdir(dir);
		if (!entry)
			break;

		if (entry->d_type == DT_REG) {
			book_entry_t be;

			if (load_sgf_opening(path + "/" + entry->d_name, &be) == false) {
				ok = false;
				break;
			}

			entries->push_back(be);
		}
	}

	closedir(dir);

	return ok;
}
