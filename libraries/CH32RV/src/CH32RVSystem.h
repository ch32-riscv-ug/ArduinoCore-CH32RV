/* The chip-level odds and ends, on one object - the ESP core's `ESP` idiom.
 *
 *     #include <CH32RV.h>
 *
 *     CH32RV.restart();                       // clean software reset
 *     if (CH32RV.resetReason() == CH32RV_RESET_WATCHDOG) { ... }
 *     Serial.println(CH32RV.resetReasonName());
 *     CH32RV.wdtEnable(2000);                 // reset unless fed every 2 s
 *     CH32RV.wdtFeed();                       // in loop()
 *
 * Design notes, decided 2026-08-25 (docs/research/system-api-esp32-style.ja.md):
 *
 *  - Modeled on ESP8266/ESP32's `ESP.*` because these functions have no
 *    Arduino-standard API and that is the convention users know.
 *  - There is NO wdtDisable(). The IWDG is irreversible by design - the
 *    start key cannot be taken back - and offering a disable that does not
 *    disable would be worse than the missing convenience. Once enabled, keep
 *    feeding or reset.
 *  - The watchdog timeout is approximate: the LSI oscillator it runs from is
 *    specified as loosely as 25..60 kHz around a typical, and the typical is
 *    what the conversion uses (CH32RV_LSI_HZ, generated per family from
 *    ch32-device-data's F_LSI). The rounding is toward a *shorter* real
 *    timeout, never a longer one.
 *  - resetReason() latches on first read and clears the hardware flags, so
 *    the answer stays stable for the sketch's lifetime and the *next* boot
 *    reads its own cause instead of an accumulation of history.
 */
#pragma once

#include <stdint.h>

#include "pins_arduino.h"   /* CH32RV_ESIG_* decide which members exist */

/* Values mirror the meaning (not the numbers) of esp_reset_reason_t. */
typedef enum {
    CH32RV_RESET_UNKNOWN = 0,
    CH32RV_RESET_POWERON,        /* power came up                        */
    CH32RV_RESET_EXTERNAL,       /* the NRST pin                         */
    CH32RV_RESET_SOFTWARE,       /* CH32RV.restart()                       */
    CH32RV_RESET_WATCHDOG,       /* the independent watchdog bit         */
    CH32RV_RESET_WINDOW_WATCHDOG,
    CH32RV_RESET_LOW_POWER,
} CH32RVResetReason;

namespace arduino {

class CH32RVSystem {
public:
    /* Reset the whole chip through the PFIC. Does not return. */
    [[noreturn]] void restart();

    /* Why this boot happened. POWERON wins over the pin flag (a power-up
     * sets both), and the watchdog and software flags win over both, since
     * they describe the most recent, most specific cause. */
    CH32RVResetReason resetReason();
    const char *resetReasonName();

    /* Start the independent watchdog: reset unless wdtFeed() is called at
     * least every `ms` (approximately - see the header comment). Clamped to
     * the hardware range; at the slowest prescaler the ceiling is around
     * half a minute depending on the family's LSI. False where this family
     * has no IWDG or its LSI frequency is not in the data (CH32X033/X035
     * today; requested upstream). Cannot be undone - see above. */
    bool wdtEnable(uint32_t ms);
    void wdtFeed();

    /* arduino-esp32's ESP.* names for the same facts. */
    /* The heap: the RAM between the end of .bss and the reserved stack. */
    uint32_t getHeapSize();
    /* What malloc() can still hand out: never-claimed heap plus freed blocks
     * (fragmented - one allocation of this size may still fail). */
    uint32_t getFreeHeap();
    uint32_t getCpuFreqMHz() { return (uint32_t)(F_CPU / 1000000UL); }
    /* The series this sketch was built for ("CH32V003"). A binary only runs on
     * its own series, so this is the chip it is running on. */
    const char *getChipModel();
    /* Flash the sketch occupies (code, constants and .data's initial values),
     * and what is left of the flash this build is linked for - the selected
     * part's, or the smallest in the series for the ANY entry. */
    uint32_t getSketchSize();
    uint32_t getFreeSketchSpace();
#ifdef CH32RV_ESIG_FLACAP_ADDR
    /* The chip's own flash size in bytes, from its electronic signature
     * (FLACAP) - the silicon's, not the menu's (device-data esig.csv). */
    uint32_t getFlashChipSize();
    /* A per-chip unique value, as sketches use ESP.getEfuseMac(): the low 64
     * bits of the 96-bit UID (UNIID1 | UNIID2 << 32). Not a MAC address. */
    uint64_t getEfuseMac();
    /* The whole 96-bit UID, UNIID1's bytes first (little-endian words). */
    void getUniqueId(uint8_t out[12]);
#endif

private:
    uint32_t _latched = 0;     /* RSTSCKR flags, once read */
    bool _read = false;
};

}  // namespace arduino

extern arduino::CH32RVSystem CH32RV;
