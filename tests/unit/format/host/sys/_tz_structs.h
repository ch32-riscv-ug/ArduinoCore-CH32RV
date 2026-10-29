/* newlib's sys/_tz_structs.h, _tzname, _timezone and _daylight, for building the core's time functions on
 * the host. */
#pragma once

#include <time.h>

typedef struct __tzrule_struct {
    char ch;
    int m, n, d, s;
    time_t change;
    long offset;
} __tzrule_type;

typedef struct __tzinfo_struct {
    int __tznorth;
    int __tzyear;
    __tzrule_type __tzrule[2];
} __tzinfo_type;

__tzinfo_type *__gettzinfo(void);
extern char *_tzname[2];
extern long _timezone;
extern int _daylight;
