/* CH32RV.setAlarm() and the rest (CH32RVSystem.h): the RTC alarm in wall
 * time, on the core's ch32rv_rtc_user_alarm(). A file of its own: linked only
 * when used. enableAlarmWakeup() is in CH32RVSleep.cpp, with the sleeps. */
#include "CH32RVSystem.h"

#include "Arduino.h"
#include "internal/ch32rv_rtc.h"

#include <time.h>

namespace arduino {

bool CH32RVSystem::setAlarm(time_t when, void (*callback)())
{
#ifdef CH32RV_HAS_RTC
    /* The counter is 32 bits of UNIX seconds (2106); 0 is "no alarm". */
    if (when <= 0 || (uint64_t)when > 0xFFFFFFFFull) {
        return false;
    }
    return ch32rv_rtc_user_alarm((uint32_t)when, callback) != 0;
#else
    (void)when;
    (void)callback;
    return false;
#endif
}

bool CH32RVSystem::setAlarm(const struct tm &local, void (*callback)())
{
    struct tm t = local;
    return setAlarm(mktime(&t), callback);
}

void CH32RVSystem::clearAlarm()
{
#ifdef CH32RV_HAS_RTC
    if (ch32rv_rtc_user_alarm_at()) {
        ch32rv_rtc_user_alarm(0, nullptr);
    }
#endif
}

time_t CH32RVSystem::getAlarm()
{
#ifdef CH32RV_HAS_RTC
    return (time_t)ch32rv_rtc_user_alarm_at();
#else
    return 0;
#endif
}

}  // namespace arduino
