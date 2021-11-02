// (C) 2021 by Folkert van Heusden <mail@vanheusden.com>
// Released under Apache License v2.0

typedef enum { debug, info, notice, warning, error } log_level_t;

void setlog(const char *lf, const log_level_t ll_file, const log_level_t ll_screen);
void closelog();
void endlogging();
void dolog(const log_level_t ll, const char *fmt, ...);
