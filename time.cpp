// (C) 2021 by Folkert van Heusden <mail@vanheusden.com>
// Released under Apache License v2.0

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "error.h"

uint64_t get_ts_ms()
{
	struct timespec ts { 0, 0 };

	if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
		error_exit(true, "clock_gettime failed");

	return uint64_t(ts.tv_sec) * uint64_t(1000) + uint64_t(ts.tv_nsec / 1000000);
}

void mymsleep(uint64_t ms)
{
        struct timespec req;

        req.tv_sec = ms / 1000;
        req.tv_nsec = (ms % 1000) * 1000000l;

        for(;;) {
                struct timespec rem { 0, 0 };

                if (nanosleep(&req, &rem) == 0)
                        break;

                memcpy(&req, &rem, sizeof(struct timespec));
        }
}
