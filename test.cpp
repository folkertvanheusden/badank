#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include "proc.h"

int main(int argc, char *argv[])
{
	TextProgram *tp = new TextProgram("/usr/games/gnugo --mode gtp --level 0", "/tmp");

	for(int i=0; i<2; i++) {
		if (i == 0)
			assert(tp->write("genmove b"));
		else
			assert(tp->write("genmove w"));

		std::optional<std::string> rc1 = tp->read(1000);
		assert(rc1.has_value());

		printf("%s\n", rc1.value().c_str());
	}

	assert(tp->write("final_score"));

	for(;;) {
		std::optional<std::string> rc2 = tp->read(1100);
		assert(rc2.has_value());

		if (rc2.value().empty() == false) {
			printf("%s\n", rc2.value().c_str());
			break;
		}
	}

	delete tp;

	printf("---\n");

	getchar();

	return 0;
}
