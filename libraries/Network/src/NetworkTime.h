/* Wall-clock time from SNTP, as ESP32's configTime() / getLocalTime().
 * The SNTP client sets the core's clock (settimeofday()); time(),
 * localtime_r() and strftime() read it. It resyncs every hour. */
#pragma once
#include <stdint.h>
#include <time.h>

/* gmtOffset_sec east of UTC (Japan: 9 * 3600); daylightOffset_sec added in
 * summer (0 for none). server2 / server3 optional; names are looked up by DNS. */
void configTime(long gmtOffset_sec, int daylightOffset_sec, const char *server1, const char *server2 = nullptr,
                const char *server3 = nullptr);
/* The same with a POSIX TZ string ("JST-9", "CET-1CEST,M3.5.0,M10.5.0/3"). */
void configTzTime(const char *tz, const char *server1, const char *server2 = nullptr, const char *server3 = nullptr);
/* The local time once the clock has been set (a year after 2016), waiting up
 * to ms for the first sync; false if it has not come. */
bool getLocalTime(struct tm *info, uint32_t ms = 5000);
