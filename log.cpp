// (C) 2021 by Folkert van Heusden <mail@vanheusden.com>
// Released under Apache License v2.0

#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "log.h"
#include "time.h"

static const char *logfile = nullptr;
static log_level_t log_level_file = warning;
static log_level_t log_level_screen = warning;
static FILE *lfh = nullptr;

void setlog(const char *lf, const log_level_t ll_file, const log_level_t ll_screen)
{
	if (lfh)
		fclose(lfh);

	free((void *)logfile);

	logfile = strdup(lf);

	log_level_file = ll_file;
	log_level_screen = ll_screen;
}

void closelog()
{
	if (lfh) {
		fclose(lfh);
		lfh = nullptr;
	}
}

void endlogging()
{
	closelog();

	free((void *)logfile);
	logfile = nullptr;
}

void dolog(const log_level_t ll, const char *fmt, ...)
{
	if (ll < log_level_file && ll < log_level_screen)
		return;

	if (!lfh && logfile) {
		lfh = fopen(logfile, "a+");
		if (!lfh) {
			fprintf(stderr, "Cannot access log-file %s: %s\n", logfile, strerror(errno));
			exit(1);
		}

		if (fcntl(fileno(lfh), F_SETFD, FD_CLOEXEC) == -1) {
			fprintf(stderr, "fcntl(FD_CLOEXEC): %s\n", strerror(errno));
			exit(1);
		}
	}

	uint64_t now = get_ts_ms();
	time_t t_now = now / 1000;

	struct tm tm { 0 };
	if (!localtime_r(&t_now, &tm))
		fprintf(stderr, "localtime_r: %s\n", strerror(errno));

	char *ts_str = nullptr;

	const char *const ll_names[] = { "debug  ", "info   ", "warning", "error  " };

	asprintf(&ts_str, "%04d-%02d-%02d %02d:%02d:%02d.%03d [%d] %s ",
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, int(now % 1000),
			gettid(), ll_names[ll]);

	char *str = nullptr;

	va_list ap;
	va_start(ap, fmt);
	(void)vasprintf(&str, fmt, ap);
	va_end(ap);

	if (ll >= log_level_file && lfh) {
		fprintf(lfh, "%s%s\n", ts_str, str);
		fflush(lfh);
	}

	if (ll >= log_level_screen)
		printf("%s%s\n", ts_str, str);

	free(str);
	free(ts_str);
}
