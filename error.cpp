// (C) 2021 by Folkert van Heusden <mail@vanheusden.com>
// Released under Apache License v2.0

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"

void error_exit(const bool se, const char *const format, ...)
{
	int e = errno;
	va_list ap;

	char *buffer = nullptr;

	va_start(ap, format);
	vasprintf(&buffer, format, ap);
	va_end(ap);

	fprintf(stderr, "%s\n", buffer);

	if (se) {
		fprintf(stderr, "%s\n", strerror(e));
		dolog(error, "%s (%s)", buffer, strerror(e));
	}
	else {
		dolog(error, "%s", buffer);
	}

	free(buffer);

	exit(1);
}
