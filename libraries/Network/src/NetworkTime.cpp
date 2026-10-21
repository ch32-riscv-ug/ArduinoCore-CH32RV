#include "NetworkTime.h"
#include "NetworkCore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "lwip/apps/sntp.h"

extern "C" void network_sntp_set_time(uint32_t sec, uint32_t us)
{
    struct timeval tv;
    tv.tv_sec = (time_t)sec;
    tv.tv_usec = (suseconds_t)us;
    settimeofday(&tv, nullptr);
}

static void start_sntp(const char *server1, const char *server2, const char *server3)
{
    ch32rv_net::start();
    if (sntp_enabled()) {
        sntp_stop();
    }
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    /* lwIP keeps the pointers: the names must outlive the client. */
    static char names[SNTP_MAX_SERVERS][64];
    const char *servers[SNTP_MAX_SERVERS] = {server1, server2, server3};
    for (u8_t i = 0; i < SNTP_MAX_SERVERS; i++) {
        if (servers[i]) {
            snprintf(names[i], sizeof names[i], "%s", servers[i]);
            sntp_setservername(i, names[i]);
        } else {
            sntp_setservername(i, nullptr);
        }
    }
    sntp_init();
}

/* POSIX TZ offsets are west of UTC: UTC+9 is "UTC-9". */
static void put_offset(char *out, size_t size, const char *name, long east)
{
    const long west = -east;
    const long a = west < 0 ? -west : west;
    if (a % 3600) {
        snprintf(out, size, "%s%s%ld:%02ld", name, west < 0 ? "-" : "", a / 3600, a % 3600 / 60);
    } else {
        snprintf(out, size, "%s%s%ld", name, west < 0 ? "-" : "", a / 3600);
    }
}

void configTime(long gmtOffset_sec, int daylightOffset_sec, const char *server1, const char *server2,
                const char *server3)
{
    char tz[48], dst[24] = "";
    put_offset(tz, sizeof tz, "UTC", gmtOffset_sec);
    if (daylightOffset_sec) {
        put_offset(dst, sizeof dst, "DST", gmtOffset_sec + daylightOffset_sec);
        strncat(tz, dst, sizeof tz - strlen(tz) - 1);
    }
    setenv("TZ", tz, 1);
    tzset();
    start_sntp(server1, server2, server3);
}

void configTzTime(const char *tz, const char *server1, const char *server2, const char *server3)
{
    setenv("TZ", tz, 1);
    tzset();
    start_sntp(server1, server2, server3);
}

bool getLocalTime(struct tm *info, uint32_t ms)
{
    const uint32_t start = millis();
    for (;;) {
        const time_t now = time(nullptr);
        localtime_r(&now, info);
        if (info->tm_year > 2016 - 1900) {
            return true;
        }
        if ((uint32_t)(millis() - start) >= ms) {
            return false;
        }
        delay(10);
    }
}
