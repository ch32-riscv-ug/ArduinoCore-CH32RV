#include "CH32RVSystem.h"

#include "Arduino.h"
#include "ch32rv_registers.h"

#include <malloc.h>
#include <stdint.h>

extern "C" {
void *_sbrk(ptrdiff_t incr);
extern char _end[];
extern char _heap_end[];
extern char _data_vma[];
extern char _data_lma[];
extern char _edata[];
extern char _ch32rv_flash_origin[];
extern char _ch32rv_flash_length[];
}

uint32_t CH32RVSystem::getHeapSize()
{
    return (uint32_t)(_heap_end - _end);
}

uint32_t CH32RVSystem::getFreeHeap()
{
    const char *brk = (const char *)_sbrk(0);
    const struct mallinfo mi = mallinfo();
    return (uint32_t)(_heap_end - brk) + (uint32_t)mi.fordblks;
}

const char *CH32RVSystem::getChipModel()
{
    return CH32RV_SERIES_NAME;
}

uint32_t CH32RVSystem::getSketchSize()
{
    return (uint32_t)((_data_lma + (_edata - _data_vma)) - _ch32rv_flash_origin);
}

#ifdef CH32RV_ESIG_FLACAP_ADDR
uint32_t CH32RVSystem::getFlashChipSize()
{
    return (uint32_t)(*(volatile const uint16_t *)CH32RV_ESIG_FLACAP_ADDR) * 1024u;
}

uint64_t CH32RVSystem::getEfuseMac()
{
    const uint32_t lo = *(volatile const uint32_t *)CH32RV_ESIG_UNIID1_ADDR;
    const uint32_t hi = *(volatile const uint32_t *)CH32RV_ESIG_UNIID2_ADDR;
    return ((uint64_t)hi << 32) | lo;
}

void CH32RVSystem::getUniqueId(uint8_t out[12])
{
    const uint32_t words[3] = {*(volatile const uint32_t *)CH32RV_ESIG_UNIID1_ADDR,
                               *(volatile const uint32_t *)CH32RV_ESIG_UNIID2_ADDR,
                               *(volatile const uint32_t *)CH32RV_ESIG_UNIID3_ADDR};
    for (int i = 0; i < 12; i++) {
        out[i] = (uint8_t)(words[i / 4] >> (8 * (i % 4)));
    }
}
#endif

uint32_t CH32RVSystem::getFreeSketchSpace()
{
    const uint32_t length = (uint32_t)(uintptr_t)_ch32rv_flash_length;
    const uint32_t used = getSketchSize();
    return used < length ? length - used : 0u;
}

namespace arduino {

void CH32RVSystem::restart()
{
    CH32RV_PFIC_CFGR = CH32RV_PFIC_KEY3 | CH32RV_PFIC_SYSRST;
    for (;;) {
        /* The write takes effect within a couple of cycles; this is only
         * here so the compiler believes [[noreturn]]. */
    }
}

CH32RVResetReason CH32RVSystem::resetReason()
{
    if (!_read) {
        _latched = CH32RV_RCC_RSTSCKR;
        CH32RV_RCC_RSTSCKR = _latched | CH32RV_RST_RMVF;
        _read = true;
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

}  // namespace arduino

arduino::CH32RVSystem CH32RV;
