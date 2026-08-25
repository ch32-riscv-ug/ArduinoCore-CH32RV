#include "CH32System.h"

#include "Arduino.h"
#include "ch32_registers.h"

namespace arduino {

void CH32System::restart()
{
    CH32_PFIC_CFGR = CH32_PFIC_KEY3 | CH32_PFIC_SYSRST;
    for (;;) {
        /* The write takes effect within a couple of cycles; this is only
         * here so the compiler believes [[noreturn]]. */
    }
}

CH32ResetReason CH32System::resetReason()
{
    if (!_read) {
        _latched = CH32_RCC_RSTSCKR;
        CH32_RCC_RSTSCKR = _latched | CH32_RST_RMVF;
        _read = true;
    }
    /* Order matters: a watchdog or software reset also leaves the pin flag
     * set on some parts, and a power-up sets the pin flag too. The most
     * specific cause wins. */
    if (_latched & CH32_RST_IWDG) {
        return CH32_RESET_WATCHDOG;
    }
    if (_latched & CH32_RST_WWDG) {
        return CH32_RESET_WINDOW_WATCHDOG;
    }
    if (_latched & CH32_RST_SFT) {
        return CH32_RESET_SOFTWARE;
    }
    if (_latched & CH32_RST_LPWR) {
        return CH32_RESET_LOW_POWER;
    }
    if (_latched & CH32_RST_POR) {
        return CH32_RESET_POWERON;
    }
    if (_latched & CH32_RST_PIN) {
        return CH32_RESET_EXTERNAL;
    }
    return CH32_RESET_UNKNOWN;
}

const char *CH32System::resetReasonName()
{
    switch (resetReason()) {
    case CH32_RESET_POWERON:         return "poweron";
    case CH32_RESET_EXTERNAL:        return "external";
    case CH32_RESET_SOFTWARE:        return "software";
    case CH32_RESET_WATCHDOG:        return "watchdog";
    case CH32_RESET_WINDOW_WATCHDOG: return "window_watchdog";
    case CH32_RESET_LOW_POWER:       return "low_power";
    default:                         return "unknown";
    }
}

bool CH32System::wdtEnable(uint32_t ms)
{
#if defined(CH32_LSI_HZ) && defined(CH32_IWDG_BASE)
    /* ticks = ms * LSI / (1000 * prescaler); find the smallest prescaler
     * whose 12-bit reload can hold the request. Smallest, because prescaler
     * granularity is what the timeout resolution costs. */
    uint32_t divider = 4;
    uint8_t field = 0;
    uint32_t ticks;
    for (;;) {
        ticks = (ms * (CH32_LSI_HZ / 1000u)) / divider;
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
    CH32_IWDG_CTLR = CH32_IWDG_KEY_UNLOCK;
    /* PSCR/RLDR writes are transferred to the LSI domain; STATR says when
     * the previous transfer is still in flight. Bounded, as every wait in
     * this core is. */
    uint32_t spin = 100000u;
    while ((CH32_IWDG_STATR & 0x3u) && --spin) { }
    CH32_IWDG_PSCR = field;
    CH32_IWDG_RLDR = (uint16_t)ticks;
    CH32_IWDG_CTLR = CH32_IWDG_KEY_FEED;
    CH32_IWDG_CTLR = CH32_IWDG_KEY_START;
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

void CH32System::wdtFeed()
{
#ifdef CH32_IWDG_BASE
    CH32_IWDG_CTLR = CH32_IWDG_KEY_FEED;
#endif
}

}  // namespace arduino

arduino::CH32System CH32;
