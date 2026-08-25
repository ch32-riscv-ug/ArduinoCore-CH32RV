/* The chip-level odds and ends, on one object - the ESP core's `ESP` idiom.
 *
 *     #include <CH32.h>
 *
 *     CH32.restart();                       // clean software reset
 *     if (CH32.resetReason() == CH32_RESET_WATCHDOG) { ... }
 *     Serial.println(CH32.resetReasonName());
 *     CH32.wdtEnable(2000);                 // reset unless fed every 2 s
 *     CH32.wdtFeed();                       // in loop()
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
 *    what the conversion uses (CH32_LSI_HZ, generated per family from
 *    ch32-device-data's F_LSI). The rounding is toward a *shorter* real
 *    timeout, never a longer one.
 *  - resetReason() latches on first read and clears the hardware flags, so
 *    the answer stays stable for the sketch's lifetime and the *next* boot
 *    reads its own cause instead of an accumulation of history.
 */
#pragma once

#include <stdint.h>

/* Values mirror the meaning (not the numbers) of esp_reset_reason_t. */
typedef enum {
    CH32_RESET_UNKNOWN = 0,
    CH32_RESET_POWERON,        /* power came up                        */
    CH32_RESET_EXTERNAL,       /* the NRST pin                         */
    CH32_RESET_SOFTWARE,       /* CH32.restart()                       */
    CH32_RESET_WATCHDOG,       /* the independent watchdog bit         */
    CH32_RESET_WINDOW_WATCHDOG,
    CH32_RESET_LOW_POWER,
} CH32ResetReason;

namespace arduino {

class CH32System {
public:
    /* Reset the whole chip through the PFIC. Does not return. */
    [[noreturn]] void restart();

    /* Why this boot happened. POWERON wins over the pin flag (a power-up
     * sets both), and the watchdog and software flags win over both, since
     * they describe the most recent, most specific cause. */
    CH32ResetReason resetReason();
    const char *resetReasonName();

    /* Start the independent watchdog: reset unless wdtFeed() is called at
     * least every `ms` (approximately - see the header comment). Clamped to
     * the hardware range; at the slowest prescaler the ceiling is around
     * half a minute depending on the family's LSI. False where this family
     * has no IWDG or its LSI frequency is not in the data (CH32X033/X035
     * today; requested upstream). Cannot be undone - see above. */
    bool wdtEnable(uint32_t ms);
    void wdtFeed();

private:
    uint32_t _latched = 0;     /* RSTSCKR flags, once read */
    bool _read = false;
};

}  // namespace arduino

extern arduino::CH32System CH32;
