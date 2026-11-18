#include "CH32RVSystem.h"

#include "Arduino.h"
#include "internal/ch32rv_registers.h"
#include "internal/ch32rv_stack.h"
#include "internal/ch32rv_rtc.h"
#include "internal/ch32rv_irq.h"

#include <stdint.h>

extern "C" {
extern char _end[];
extern char _heap_end[];
extern char _ch32rv_image_end[];       /* the end of the image in flash, every layout */
extern char _ch32rv_flash_origin[];
extern char _ch32rv_flash_length[];
#ifdef CH32RV_CLOCK_HSE_HZ
extern uint8_t ch32rv_clock_on_hse;   /* wiring_time.c */
#endif
}

uint32_t CH32RVSystem::getHeapSize()
{
    return (uint32_t)(_heap_end - _end);
}

const char *CH32RVSystem::getChipModel()
{
    return CH32RV_SERIES_NAME;
}

uint32_t CH32RVSystem::getSketchSize()
{
    /* _data_lma does not exist in the LOADCODE layouts (CH32H41x), which failed
     * to link any sketch calling this. */
    return (uint32_t)(_ch32rv_image_end - _ch32rv_flash_origin);
}

#ifdef CH32RV_ESIG_FLACAP_ADDR
uint32_t CH32RVSystem::getFlashChipSize()
{
    return (uint32_t)(*(volatile const uint16_t *)CH32RV_ESIG_FLACAP_ADDR) * 1024u;
}

uint64_t CH32RVSystem::getUniqueId()
{
    const uint32_t lo = *(volatile const uint32_t *)CH32RV_ESIG_UNIID1_ADDR;
    const uint32_t hi = *(volatile const uint32_t *)CH32RV_ESIG_UNIID2_ADDR;
    return ((uint64_t)hi << 32) | lo;
}
#endif

uint32_t CH32RVSystem::getFreeSketchSpace()
{
    const uint32_t length = (uint32_t)(uintptr_t)_ch32rv_flash_length;
    const uint32_t used = getSketchSize();
    return used < length ? length - used : 0u;
}

namespace arduino {

/* Weak: the Update library's takes its place, installing a pending update first. */
__attribute__((weak)) void CH32RVSystem::restart()
{
    CH32RV_SYSTEM_RESET();
    for (;;) {
        /* The write takes effect within a couple of cycles; this is only
         * here so the compiler believes [[noreturn]]. */
    }
}

/* Set by the sleep code's boot hook (ch32rv_power.c) when this boot is the
 * wake from standby; weak, so a sketch that never sleeps links none of it. */
extern "C" volatile uint8_t ch32rv_power_standby_woke __attribute__((weak));

CH32RVResetReason CH32RVSystem::resetReason()
{
    if (!_read) {
        _latched = CH32RV_RCC_RSTSCKR;
        CH32RV_RCC_RSTSCKR = _latched | CH32RV_RST_RMVF;
        /* The core's own reset after a stack overflow (ch32rv_stack.h): a
         * software reset with the record set just before it. */
        _panic = (_latched & CH32RV_RST_SFT) && ch32rv_stack_panicked;
        _read = true;
    }
    if (_panic) {
        return CH32RV_RESET_PANIC;
    }
    /* Standby's wake leaves the power-on flag (V307), the wake-up flag only
     * (V203) or nothing (V103): the sleep code tells it from those. */
    if (&ch32rv_power_standby_woke && ch32rv_power_standby_woke) {
        return CH32RV_RESET_DEEPSLEEP;
    }
    /* Order matters: a watchdog or software reset also leaves the pin flag
     * set on some parts, and a power-up sets the pin flag too. The most
     * specific cause wins. */
    if (_latched & CH32RV_RST_IWDG) {
        return CH32RV_RESET_WATCHDOG;
    }
    if (_latched & CH32RV_RST_WWDG) {
        return CH32RV_RESET_WINDOW_WATCHDOG;
    }
    if (_latched & CH32RV_RST_SFT) {
        return CH32RV_RESET_SOFTWARE;
    }
    if (_latched & CH32RV_RST_LPWR) {
        return CH32RV_RESET_LOW_POWER;
    }
    if (_latched & CH32RV_RST_POR) {
        return CH32RV_RESET_POWERON;
    }
    if (_latched & CH32RV_RST_PIN) {
        return CH32RV_RESET_EXTERNAL;
    }
    return CH32RV_RESET_UNKNOWN;
}

const char *CH32RVSystem::resetReasonName()
{
    switch (resetReason()) {
    case CH32RV_RESET_POWERON:         return "poweron";
    case CH32RV_RESET_EXTERNAL:        return "external";
    case CH32RV_RESET_SOFTWARE:        return "software";
    case CH32RV_RESET_WATCHDOG:        return "watchdog";
    case CH32RV_RESET_WINDOW_WATCHDOG: return "window_watchdog";
    case CH32RV_RESET_LOW_POWER:       return "low_power";
    case CH32RV_RESET_PANIC:           return "panic";
    case CH32RV_RESET_DEEPSLEEP:       return "deepsleep";
    default:                         return "unknown";
    }
}

bool CH32RVSystem::wdtEnable(uint32_t ms)
{
#if defined(CH32RV_LSI_HZ) && defined(CH32RV_IWDG_BASE)
    /* ticks = ms * LSI / (1000 * prescaler); find the smallest prescaler
     * whose 12-bit reload can hold the request. Smallest, because prescaler
     * granularity is what the timeout resolution costs. */
    uint32_t divider = 4;
    uint8_t field = 0;
    uint32_t ticks;
    for (;;) {
        ticks = (ms * (CH32RV_LSI_HZ / 1000u)) / divider;
        if (ticks <= 0xFFFu || divider == 256u) {
            break;
        }
        divider *= 2;
        field++;
    }
    if (ticks > 0xFFFu) {
        ticks = 0xFFFu;              /* longest this hardware can do */
    }
    if (ticks == 0u) {
        ticks = 1u;
    }
    CH32RV_IWDG_CTLR = CH32RV_IWDG_KEY_UNLOCK;
    /* PSCR/RLDR writes are transferred to the LSI domain; STATR says when
     * the previous transfer is still in flight. Bounded, as every wait in
     * this core is. */
    uint32_t spin = 100000u;
    while ((CH32RV_IWDG_STATR & 0x3u) && --spin) { }
    CH32RV_IWDG_PSCR = field;
    CH32RV_IWDG_RLDR = (uint16_t)ticks;
    CH32RV_IWDG_CTLR = CH32RV_IWDG_KEY_FEED;
    CH32RV_IWDG_CTLR = CH32RV_IWDG_KEY_START;
    _wdtOn = true;
    _wdtField = field;
    _wdtTicks = (uint16_t)ticks;
    return true;
#else
    /* Either this family has no IWDG block at all (CH32M030), or its F_LSI
     * is missing from the device data (X033/X035 today; requested upstream)
     * and a millisecond argument would be a made-up conversion. Honestly
     * unavailable rather than approximately wrong. */
    (void)ms;
    return false;
#endif
}

void CH32RVSystem::wdtFeed()
{
#ifdef CH32RV_IWDG_BASE
    CH32RV_IWDG_CTLR = CH32RV_IWDG_KEY_FEED;
#endif
}

CH32RVRtcClock CH32RVSystem::rtcClock()
{
    if (!ch32rv_rtc_begin()) {
        return CH32RV_RTC_CLOCK_NONE;
    }
    return ch32rv_rtc_on_lse() ? CH32RV_RTC_CLOCK_LSE : CH32RV_RTC_CLOCK_LSI;
}

#if defined(CH32RV_SERIES_CH32X305) || defined(CH32RV_SERIES_CH32X315)
#define CH32RV_NO_HSITRIM 1
#endif
#define HSITRIM_SHIFT 3u
#define HSITRIM_MASK  (0x1Fu << HSITRIM_SHIFT)

uint8_t CH32RVSystem::hsiTrim()
{
#ifdef CH32RV_NO_HSITRIM
    return 0;
#else
    return (uint8_t)((CH32RV_RCC_CTLR & HSITRIM_MASK) >> HSITRIM_SHIFT);
#endif
}

bool CH32RVSystem::setHsiTrim(uint8_t trim)
{
#ifdef CH32RV_NO_HSITRIM
    (void)trim;
    return false;
#else
    if (trim > 31u) {
        return false;
    }
    CH32RV_RCC_CTLR = (CH32RV_RCC_CTLR & ~HSITRIM_MASK) | ((uint32_t)trim << HSITRIM_SHIFT);
    return true;
#endif
}

#ifndef CH32RV_NO_HSITRIM
/* HSI's error in ppm (+ fast): micros() counts HSI (through the PLL), the
 * RTC counts the crystal; over `ms` the two are compared. */
static int32_t hsi_error_ppm(uint32_t ms)
{
    const uint32_t irq = ch32rv_irq_save();
    const uint64_t l0 = ch32rv_rtc_lse_ticks();
    const uint32_t t0 = micros();
    ch32rv_irq_restore(irq);
    while (micros() - t0 < ms * 1000u) {
    }
    const uint32_t irq2 = ch32rv_irq_save();
    const uint64_t l1 = ch32rv_rtc_lse_ticks();
    const uint32_t t1 = micros();
    ch32rv_irq_restore(irq2);
    /* (t / real - 1) x 10^6 with real = ticks x 10^6 / 32768 */
    const int64_t ticks = (int64_t)(l1 - l0);
    if (ticks <= 0) {
        return 0;
    }
    return (int32_t)(((int64_t)(t1 - t0) * 32768 - ticks * 1000000) / ticks);
}
#endif

bool CH32RVSystem::calibrateHsi(int32_t *errorPpm)
{
#ifdef CH32RV_NO_HSITRIM
    (void)errorPpm;
    return false;
#else
    if (clockSource() != CH32RV_CLOCK_SOURCE_HSI || rtcClock() != CH32RV_RTC_CLOCK_LSE) {
        return false;
    }
    /* Walk the trim toward zero error a step at a time on short windows (a
     * step is far larger than their resolution), then keep the best of the
     * last two and measure it longer. */
    uint8_t trim = hsiTrim();
    int32_t err = hsi_error_ppm(100);
    for (uint8_t i = 0; i < 32u; i++) {
        const int dir = err > 0 ? -1 : 1;
        const int next = (int)trim + dir;
        if (next < 0 || next > 31) {
            break;
        }
        setHsiTrim((uint8_t)next);
        delay(2);                                  /* HSI and the PLL settle */
        const int32_t e = hsi_error_ppm(100);
        if ((e > 0) != (err > 0) || e == 0) {     /* crossed zero: the nearer of the two */
            if ((e < 0 ? -e : e) > (err < 0 ? -err : err)) {
                setHsiTrim(trim);
                delay(2);
            } else {
                trim = (uint8_t)next;
            }
            break;
        }
        trim = (uint8_t)next;
        err = e;
    }
    if (errorPpm) {
        *errorPpm = hsi_error_ppm(500);
    }
    return true;
#endif
}

CH32RVClockSource CH32RVSystem::clockSource()
{
#ifdef CH32RV_CLOCK_HSE_HZ
    return ch32rv_clock_on_hse ? CH32RV_CLOCK_SOURCE_HSE : CH32RV_CLOCK_SOURCE_HSI;
#else
    return CH32RV_CLOCK_SOURCE_HSI;
#endif
}


}  // namespace arduino

arduino::CH32RVSystem CH32RV;

uint32_t CH32RVSystem::getStackSize()
{
    return ch32rv_stack_size();
}

uint32_t CH32RVSystem::getMinFreeStack()
{
    return ch32rv_stack_min_free();
}
