/* CH32RV.lightSleep() / deepSleep() (CH32RVSystem.h). A file of its own, so
 * that a sketch that never sleeps links none of the low-power code (this
 * library is linked as an archive). */
#include "CH32RVSystem.h"

#include "Arduino.h"
#include "internal/ch32rv_power.h"
#include "internal/ch32rv_registers.h"

namespace arduino {

/* ------------------------------------------------------------------ sleep */
/* The wake sources and causes live in the core (ch32rv_power.c), so that a
 * sketch that never sleeps carries none of them. Only the watchdog's setting
 * is here, since wdtEnable() is. */

namespace {
#if defined(CH32RV_IWDG_BASE) && defined(CH32RV_LSI_HZ)
void wdtWrite(uint8_t field, uint16_t ticks)
{
    CH32RV_IWDG_CTLR = CH32RV_IWDG_KEY_UNLOCK;
    uint32_t spin = 100000u;
    while ((CH32RV_IWDG_STATR & 0x3u) && --spin) { }
    CH32RV_IWDG_PSCR = field;
    CH32RV_IWDG_RLDR = ticks;
    spin = 100000u;
    while ((CH32RV_IWDG_STATR & 0x3u) && --spin) { }
    CH32RV_IWDG_CTLR = CH32RV_IWDG_KEY_FEED;
}
#endif
}  // namespace

bool CH32RVSystem::enableTimerWakeup(uint64_t us)
{
    return ch32rv_power_set_timer(us);
}


bool CH32RVSystem::enableAlarmWakeup()
{
    return ch32rv_power_set_alarm() != 0;
}

void CH32RVSystem::disableWakeup()
{
    ch32rv_power_clear();
}

CH32RVSleepResult CH32RVSystem::lightSleep()
{
    uint32_t slice_us = 0;
#if defined(CH32RV_IWDG_BASE) && defined(CH32RV_LSI_HZ)
    if (_wdtOn && ch32rv_power_supported()) {
        /* The longest the IWDG can wait (prescaler /256, reload 4095), and
         * half of it per sleep period: the LSI may run fast. */
        wdtWrite(6, 0xFFFu);
        slice_us = (uint32_t)(4095ull * 256u * 1000000u / CH32RV_LSI_HZ / 2u);
    }
#endif
    const int r = ch32rv_power_sleep(slice_us);
#if defined(CH32RV_IWDG_BASE) && defined(CH32RV_LSI_HZ)
    if (slice_us) {
        wdtWrite(_wdtField, _wdtTicks);
    }
#endif
    return r == 0 ? CH32RVSleepResult::Ok : r == 1 ? CH32RVSleepResult::NoWakeupSource : CH32RVSleepResult::Unsupported;
}

CH32RVSleepResult CH32RVSystem::deepSleep()
{
    /* Real standby where there is one and nothing needs it to resume: the
     * RTC families, woken by the timer alone, with no watchdog to feed. */
    if (!_wdtOn && ch32rv_power_can_standby()) {
        ch32rv_power_standby();
    }
    const CH32RVSleepResult r = lightSleep();
    if (r != CH32RVSleepResult::Ok) {
        return r;
    }
    ch32rv_power_keep_cause();
    restart();
}

CH32RVWakeupCause CH32RVSystem::wakeupCause()
{
    /* The same numbers on both sides: a cause is passed as it is. */
    static_assert((int)CH32RVWakeupCause::Timer == (int)CH32RV_POWER_WOKE_TIMER &&
                  (int)CH32RVWakeupCause::Pin == (int)CH32RV_POWER_WOKE_PIN &&
                  (int)CH32RVWakeupCause::Alarm == (int)CH32RV_POWER_WOKE_ALARM &&
                  (int)CH32RVWakeupCause::ExtiLine == (int)CH32RV_POWER_WOKE_EXTI, "wake cause numbers");
    const uint8_t cause = ch32rv_wake_cause;
    return cause <= CH32RV_POWER_WOKE_EXTI ? (CH32RVWakeupCause)cause : CH32RVWakeupCause::Undefined;
}

uint32_t CH32RVSystem::timeDomain()
{
    return ch32rv_time_domain;
}

}  // namespace arduino
