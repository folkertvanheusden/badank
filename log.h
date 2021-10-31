// (C) 2021 by folkert van heusden <mail@vanheusden.com>, released under Apache License v2.0
typedef enum { debug, info, warning, error } log_level_t;

void setlog(const char *lf, const log_level_t ll_file, const log_level_t ll_screen);
void closelog();
void endlogging();
void dolog(const log_level_t ll, const char *fmt, ...);
