#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include "gtp.h"

int main(int argc, char *argv[])
{
	GtpEngine *ge = new GtpEngine("/usr/games/gnugo --mode gtp --level 0", "/tmp");

	ge->boardsize(9);
	ge->clearboard();

	auto rc = ge->getname();
	assert(rc.has_value());
	printf("name: %s\n", rc.value().c_str());

	bool rc2 = ge->play(C_BLACK, "a1");
	assert(rc2);

	auto rc3 = ge->genmove(C_WHITE, { });
	assert(rc3.has_value());
	printf("move: %s\n", rc3.value().c_str());

	delete ge;

	printf("---\n");

	getchar();

	return 0;
}
