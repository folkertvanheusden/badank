#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void error_exit(const bool se, const char *const format, ...)
{
	int e = errno;
	va_list ap;

	char *buffer = nullptr;

	va_start(ap, format);
	vasprintf(&buffer, format, ap);
	va_end(ap);

	fprintf(stderr, "%s\n", buffer);

	free(buffer);

	if (se)
		fprintf(stderr, "%s\n", strerror(e));

	exit(1);
}
