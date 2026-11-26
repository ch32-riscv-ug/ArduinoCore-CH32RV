/* The chip-level odds and ends, on one object - the ESP core's `ESP` idiom.
 *
 *     #include <CH32RV.h>
 *
 *     CH32RV.restart();                       // clean software reset
 *     if (CH32RV.resetReason() == CH32RVResetReason::Watchdog) { ... }
 *     Serial.println(CH32RV.resetReasonName());
 *     CH32RV.wdtEnable(2000);                 // reset unless fed every 2 s
 *     CH32RV.wdtFeed();                       // in loop()
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
 *    ch32-device-data's F_LSI; X033/X035 have no LSI and clock the IWDG from
 *    HSI / 1024 instead). The rounding is toward a *shorter* real timeout,
 *    never a longer one.
 *  - resetReason() latches on first read and clears the hardware flags, so
 *    the answer stays stable for the sketch's lifetime and the *next* boot
 *    reads its own cause instead of an accumulation of history.
 *
 * Sleep, with ESP32's meanings (esp_sleep_*) under these names:
 *
 *     CH32RV.enableTimerWakeup(5 * 1000000ULL);   // in 5 s
 *     CH32RV.enablePinWakeup(PA1, LOW);           // or when PA1 reads LOW
 *     CH32RV.lightSleep();                        // returns after waking
 *     CH32RV.deepSleep();                         // starts again from setup()
 *     if (CH32RV.wakeupCause() == CH32RVWakeupCause::Timer) { ... }
 *
 *  - lightSleep() keeps RAM and the pins' states and returns. deepSleep()
 *    starts again from setup(): in standby on the RTC families when only the
 *    timer wakes it and the watchdog is off; otherwise it sleeps as
 *    lightSleep() does and restarts (V00x and X035 have no standby that
 *    wakes through a reset).
 *  - Nothing sleeps by itself: only these calls do (docs/peripheral-a-design
 *    .ja.md 4.3). Serial sends what it holds before the clock stops.
 *  - idle(ms): the CPU alone waits (WFI, the core's sleep mode) for ms
 *    milliseconds, the peripherals, their DMA and interrupts running on -
 *    Serial receives, timers and PWM run, USB stays connected - as
 *    STM32LowPower's / Arduino-LowPower's LowPower.idle(ms). Each interrupt
 *    (SysTick's every millisecond among them) wakes the CPU, runs and
 *    idles again; idle() with no ms returns after the next one. On V103
 *    SysTick does not end a WFI, so idle() waits there awake.
 *  - On the AWU families the first sleep of a boot measures the AWU's clock
 *    first: about 40 ms more than asked (four 8 ms periods and a phase).
 *  - millis() keeps counting through a timer wake: the sleep is measured and
 *    added. A pin wake part-way cannot be measured, and timeDomain() moves on.
 *  - The watchdog looks paused while asleep: it cannot be stopped, so it is
 *    stretched to its longest and fed between sleep periods - nothing else
 *    runs - and put back as wdtEnable() set it on waking.
 *  - V003, V00x (V002-V007, M007), M030 and X033 / X035 wake on the AWU;
 *    V103, V20x, V205, V30x, V4x7, L103 / M103, X305 / X315 and H41x on the
 *    RTC alarm (they sleep in stop, and millis() is timed by the RTC, a pin
 *    wake included). M030's AWU counts about 150 ms at a time: what is left
 *    under that is slept in a plain WFI. CH32RVSleepResult::Unsupported remains for
 *    a family added later without its sleep.
 *
 * The window watchdog (WWDG), beside the independent one:
 *
 *     CH32RV.wwdgEnable(20000, 10000);      // feed between 10 and 20 ms after the last feed
 *     CH32RV.wwdgFeed();
 *
 *  - It runs on PCLK1 / 4096 / 1..8: at most wwdgMaxTimeoutUs() - 63 counts at
 *    the slowest prescaler, about 43 ms at 48 MHz, 258 ms at 8 MHz. Both
 *    edges are exact to one count: the reset comes at most one count before
 *    the timeout (never later), and a feed more than a count before the
 *    window opens resets the chip (from windowUs on it is always allowed).
 *  - Like the IWDG it cannot be stopped once started, and its early-warning
 *    interrupt cannot be turned off. The callback runs in that interrupt one
 *    count before the reset: feed there, or record why (the reset follows).
 *  - resetReason() tells its reset (CH32RVResetReason::WindowWatchdog) from the
 *    IWDG's (CH32RVResetReason::Watchdog).
 *  - Sleeps do not feed it. Its clock stops in stop / standby, so it waits
 *    there; the parts of a sleep spent in a plain WFI (under a second on the
 *    RTC families, under 150 ms on M030) count, and reset the chip if it was
 *    not fed just before.
 *
 * The RTC (V103, V20x, V205, V30x, V4x7, L103 / M103, X305 / X315, H41x): the
 * calendar is POSIX's - time(), localtime_r(), mktime(), settimeofday() - as
 * on arduino-esp32. One alarm, in the same wall time:
 *
 *     CH32RV.setAlarm(time(nullptr) + 60, onAlarm);   // or a struct tm (local time)
 *     CH32RV.enableAlarmWakeup();                      // and let it end a sleep
 *
 *  - The callback runs in the RTC alarm's interrupt, once, within a second
 *    after `when` (the RTC counts whole seconds); setAlarm() again for the
 *    next. An alarm already due runs at once. settimeofday() keeps it at its
 *    wall time.
 *  - enableAlarmWakeup(): lightSleep() / deepSleep() end at the alarm, and
 *    wakeupCause() says CH32RVWakeupCause::Alarm when it came before the timer.
 *    deepSleep() woken by the alarm alone is a real standby (not on X305 /
 *    X315 and H41x, which have none); the callback does not run after a
 *    standby (RAM is lost) - wakeupCause() says why it started.
 *
 * The EXTI lines that are not pins - the PVD, the RTC alarm, the USB / ETH /
 * LPTIM / comparator wake-ups - by name (no arduino-esp32 counterpart):
 *
 *     CH32RV.attachExtiLine(CH32RV_EXTI_USBFS_WAKEUP, onWake);   // RISING
 *     CH32RV.enableExtiLineWakeup(CH32RV_EXTI_USBFS_WAKEUP);    // ends a sleep too
 *     CH32RV.setLowVoltageDetect(3000, onLow);                  // VDD fell below ~3 V
 *
 *  - CH32RV_EXTI_<NAME> is the line's number, defined only where the series
 *    has the line and a vector for it (`#ifdef CH32RV_EXTI_PVD`); the list per
 *    series is in internal/ch32rv_exti_line.h. Another number is refused.
 *  - The callback runs in the line's interrupt on each edge (RISING, FALLING
 *    or CHANGE; EXTI has no levels). triggerExtiLine() raises it from
 *    software (SWIEVR) as an edge would.
 *  - CH32RV_EXTI_RTC_ALARM shares its vector with setAlarm() and the sleeps:
 *    the callback sees setAlarm()'s alarm (and any other), not the alarms a
 *    sleep sets for itself; it keeps the rising edge they need, and
 *    detachExtiLine() leaves the line on for them.
 *  - enableExtiLineWakeup(): lightSleep() / deepSleep() end on the line's
 *    edge too (CH32RVWakeupCause::ExtiLine); deepSleep() is then a light sleep and
 *    a restart, as with a pin. The AWU's line is the sleeps' own and not
 *    offered.
 *  - setLowVoltageDetect(mv, cb): the PVD at the lowest threshold at or above
 *    mv (the highest when mv is above them all: the datasheet's typical
 *    falling-edge values, a few per series), cb on each fall below it - at
 *    once when VDD already is. Returns the threshold in mV, 0 where there is
 *    no PVD interrupt (CH32H41x, CH32M030; V004 / V007 / M007 have no levels
 *    in the data). It is a warning, not ESP32's brownout reset: the chip
 *    keeps running. isLowVoltage() reads the PVD's output now.
 *
 * CRC-32 over a buffer, the CRC block's (CRC-32/MPEG-2):
 *
 *     uint32_t crc = CH32RV.crc32(buf, len);
 *     crc = CH32RV.crc32(more, n, crc);        // the same as over both at once
 *
 *  - The hardware where there is a CRC block (V103, V20x, V205, V30x, V4x7,
 *    L103 / M103, X305 / X315, H41x), a 64-byte table elsewhere: the same
 *    numbers everywhere (ch32rv_crc.h).
 *
 * Backup registers: 16-bit words that survive a reset and standby.
 *
 *     CH32RV.setBackupRegister(0, bootCount + 1);
 *     CH32RV.getBackupRegister(0);
 *
 *  - BKP DR5 and up (DR1-DR4 are the RTC's): 6 words on V103, V205 and
 *    L103 / M103, 6 or 38 on V20x, V30x and V4x7 (read from the part). They
 *    keep VBAT's power: a power cycle with a battery on VBAT keeps them.
 *  - H41x and X305 / X315 have no BKP data registers: 16 words of .noinit RAM
 *    with a check word instead - kept through every reset, lost with power
 *    (backupRegistersKeepPower() says false), zeroed after a power-up.
 *  - None on V003, V00x, M030 and X033 / X035: the count is 0.
 */
#pragma once

#include <stdint.h>

#include "pins_arduino.h"   /* CH32RV_ESIG_* decide which members exist */
#include "internal/ch32rv_vio18.h"
#include "internal/ch32rv_v3f.h"
#include "internal/ch32rv_exti_line.h"   /* CH32RV_EXTI_<line>: attachExtiLine()'s names */

#include <Arduino.h>          /* ch32rv_adc_vdd_millivolts(), declared where the CPU Clock may forbid it */
#include "ch32rv_crc.h"
#include "ch32rv_multicore.h"
#include <time.h>

/* setPinSpeed() */
enum class CH32RVPinSpeed : uint8_t {
    Low = 0,
    Medium,
    High,
    VeryHigh,
};

/* resetReason(): the meaning (not the numbers) of esp_reset_reason_t. */
enum class CH32RVResetReason : uint8_t {
    Unknown = 0,
    PowerOn,                     /* power came up                        */
    External,                    /* the NRST pin                         */
    Software,                    /* CH32RV.restart()                     */
    Watchdog,                    /* the independent watchdog bit         */
    WindowWatchdog,
    LowPower,
    Panic,                       /* the core reset itself: the Stack Check menu saw an overflow */
    DeepSleep,                   /* woke from deepSleep()'s standby (ESP_RST_DEEPSLEEP); where
                                  * deepSleep() is a light sleep and a restart, Software */
};

/* lightSleep() / deepSleep() */
enum class CH32RVSleepResult : uint8_t {
    Ok = 0,                      /* slept and woke                      */
    NoWakeupSource,              /* nothing enabled that could wake it  */
    Unsupported,                 /* not on this family                  */
};

/* wakeupCause(): ESP32's esp_sleep_wakeup_cause_t, for the sources this core has. */
enum class CH32RVWakeupCause : uint8_t {
    Undefined = 0,               /* not woken from sleep (a reset)      */
    Timer,
    Pin,
    Alarm,                       /* the RTC alarm (enableAlarmWakeup()) */
    ExtiLine,                    /* an EXTI line (enableExtiLineWakeup()) */
};

/* rtcClock(): what the RTC counts. */
enum class CH32RVRtcClock : uint8_t {
    None = 0,                    /* no RTC on this series                  */
    Lsi,                         /* the internal oscillator, measured      */
    Lse,                         /* the 32.768 kHz crystal (RTC Clock menu) */
};

/* setRtcOutput(): what the RTC puts out on the TAMPER pad (PC13 on most series). */
enum class CH32RVRtcOutput : uint8_t {
    None = 0,                    /* the pad is a GPIO again                 */
    Clock,                       /* the RTC clock / 64: 512 Hz from the LSE */
    Alarm,                       /* a pulse at the alarm                    */
    Second,                      /* a pulse each second                     */
};

/* clockSource(): where SYSCLK comes from. */
enum class CH32RVClockSource : uint8_t {
    Hsi = 0,                     /* the internal oscillator               */
    Hse,                         /* the crystal (Clock Source menu)        */
};

namespace arduino {

class CH32RVSystem {
public:

    /* Reset the whole chip through the PFIC. Does not return. */
    [[noreturn]] void restart();

    /* Turn the debug interface off until the next reset, so its pads (X035's
     * PC18 / PC19, V003's PD1, V20x / V30x's PA13 / PA14, ...) work as
     * ordinary pads - for I2C, SPI, a UART or GPIO. Until it is called, a
     * route chosen by those pins (Wire.begin(sda, scl), setPins(), setRoute())
     * returns false. Afterwards no WCH-Link or OEP probe can attach to the
     * running sketch: upload over USB, or catch the chip right after a reset
     * (CH32RV_BOOT_HOLD_MS) - on V00x the debug wire is the only way in. */
    void releaseDebugPins();
    /* Whether releaseDebugPins() has run since reset. */
    bool debugPinsReleased();

    /* Why this boot happened. POWERON wins over the pin flag (a power-up
     * sets both), and the watchdog and software flags win over both, since
     * they describe the most recent, most specific cause. */
    CH32RVResetReason resetReason();
    const char *resetReasonName();

    /* Start the independent watchdog: reset unless wdtFeed() is called at
     * least every `ms` (approximately - see the header comment). Clamped to
     * the hardware range; at the slowest prescaler the ceiling is around
     * half a minute depending on the family's LSI. False where this family
     * has no IWDG (CH32M030). Cannot be undone - see above. */
    bool wdtEnable(uint32_t ms);
    void wdtFeed();

    /* Start the window watchdog: reset unless wwdgFeed() comes within
     * timeoutUs of the last feed, and (windowUs > 0) not before windowUs
     * after it - to one count, see above. earlyWarning runs in the interrupt
     * one count before a reset. False (nothing started) when the timeout is
     * beyond wwdgMaxTimeoutUs() or under one count at the fastest prescaler,
     * or no count is left between the window and the timeout. Every family
     * has the block. Cannot be undone. */
    bool wwdgEnable(uint32_t timeoutUs, uint32_t windowUs = 0, void (*earlyWarning)() = nullptr);
    void wwdgFeed();
    /* The longest timeout at this build's PCLK1. */
    uint32_t wwdgMaxTimeoutUs();

    /* The sketch's backup words (above): how many, and read / write by index
     * from 0. False / 0 past the count and where there are none. */
    uint8_t backupRegisterCount();
    uint16_t getBackupRegister(uint8_t index);
    bool setBackupRegister(uint8_t index, uint16_t value);
    /* True where they are BKP registers (VBAT keeps them); false where they
     * are RAM (H41x, X305 / X315) or absent. */
    bool backupRegistersKeepPower();

    /* The RTC alarm at `when` (time()'s seconds), or a local time; callback
     * may be nullptr. Replaces the one set before. False where there is no
     * RTC (V003, V00x, M030, X033 / X035) or `when` is not a time. */
    bool setAlarm(time_t when, void (*callback)() = nullptr);
    bool setAlarm(const struct tm &local, void (*callback)() = nullptr);
    void clearAlarm();
    /* The alarm still to come, or 0 (none, or it has run). */
    time_t getAlarm();

    /* An EXTI line that is not a pin (CH32RV_EXTI_*, above): callback (may be
     * nullptr) on its edges. False for a line this series does not have or a
     * level mode. */
    bool attachExtiLine(uint8_t line, void (*callback)(), PinStatus mode = RISING);
    void detachExtiLine(uint8_t line);
    /* The line's interrupt from software, as an edge (EXTI_SWIEVR). */
    void triggerExtiLine(uint8_t line);
    /* The PVD: callback when VDD falls below the threshold nearest above
     * millivolts. Returns the threshold (mV), 0 where there is none. */
    uint32_t setLowVoltageDetect(uint32_t millivolts, void (*callback)() = nullptr);
    void clearLowVoltageDetect();
    /* VDD is below setLowVoltageDetect()'s threshold now. */
    bool isLowVoltage();
    /* The threshold of each PVD level from 0, lowest first; 0 past the last. */
    uint32_t lowVoltageLevel(uint8_t index);

    /* HSE when the Clock Source menu chose the crystal and it started; HSI
     * otherwise - also when the crystal was chosen and did not start, in
     * which case the same CPU Clock runs off HSI (F_CPU is unchanged, its
     * accuracy is HSI's). */
    CH32RVClockSource clockSource();
    /* LSE when the RTC Clock menu chose the crystal and it started; LSI
     * otherwise (the RTC started here if it was not yet: up to 2 s for the
     * crystal); NONE where the series has no RTC. */
    CH32RVRtcClock rtcClock();

    /* HSI's trim (RCC_CTLR HSITRIM, 0 - 31, 16 the factory's middle; each
     * step moves HSI by a few tenths of a percent). False on X305 / X315,
     * which have no trim, and for a value over 31. */
    uint8_t hsiTrim();
    bool setHsiTrim(uint8_t trim);
    /* HSI measured against the RTC's LSE crystal and its trim set to the
     * step nearest the right frequency (about 2 s); `errorPpm` (if given) is
     * what is left, + fast / - slow. False when the system clock is not HSI
     * (clockSource()) or the RTC does not count LSE (rtcClock(): the RTC
     * Clock menu's LSE, started) - there is nothing to measure against - and
     * on X305 / X315. */
    bool calibrateHsi(int32_t *errorPpm = nullptr);
    /* The LSE RTC made `ppm` slower (faster when negative): measured fast by
     * 20 ppm, setRtcCalibration(20). The prescaler moves in 30.5 ppm steps
     * and, on the series with BKP_OCTLR's CAL, CAL takes the rest in 0.95 ppm
     * units (H41x, X305 / X315 and V103, whose CAL slows nothing: the
     * prescaler alone). Kept across resets
     * with the RTC. False when the RTC does not count LSE (an LSI RTC is
     * measured at its start instead) or for more than 500000 ppm. */
    bool setRtcCalibration(int32_t ppm);
    /* The RTC clock / 64, the alarm or the second pulse on the TAMPER pad
     * (512 Hz from the LSE: what setRtcCalibration() is measured against).
     * False where there is no such output (H41x) or no RTC. */
    bool setRtcOutput(CH32RVRtcOutput what);
    /* A clock on the MCO pad (CH32RV_MCO_PIN: PA8, PC4, PB9 ... per
     * series): CH32RV_MCO_SYSCLK, _HSI, _HSE, _PLL and the others the series
     * names (the variant's CH32RV_MCO_*, from the reference manual's MCO
     * field), or 0 to stop it (the pad an input again). The source must be
     * running (HSE only with a crystal in use). False where the series or
     * part has no MCO pad, or for a value that is not one of its sources. */
    bool setClockOutput(uint8_t source);

    /* Wake after `us` microseconds (0: no timer). False where the family
     * cannot sleep yet. */
    bool enableTimerWakeup(uint64_t us);
    /* Wake when `pin` reads `level` (HIGH or LOW); up to four pins, each its
     * own EXTI line (the pin's bit). A pin already at its level makes the
     * next sleep return at once. attachInterrupt() on the same pin keeps
     * working. */
    bool enablePinWakeup(int pin, int level);
    /* setAlarm()'s alarm ends a sleep too (CH32RVWakeupCause::Alarm). False where
     * there is no RTC. */
    bool enableAlarmWakeup();
    /* An EXTI line that is not a pin (attachExtiLine()'s; its callback kept)
     * ends a sleep on its edge too. False where the family cannot sleep or
     * the series has no such line. */
    bool enableExtiLineWakeup(uint8_t line, PinStatus mode = RISING);
    void disableWakeup();
    CH32RVSleepResult lightSleep();
    void idle(uint32_t ms = 0);
    /* An output pad's slew rate (no arduino-esp32 counterpart): LOW (2 MHz),
     * MEDIUM (10 MHz, what pinMode(OUTPUT) sets), HIGH (50 MHz; 30 MHz on
     * V003); X305 / X315 have four steps of their own (VERY_HIGH the fourth,
     * pads 0-15). After pinMode() or a peripheral's begin(), which set the
     * pad's mode again. False for an input, or on X033 / X035, M030 and V00x
     * (V002 / V004 / V005 / V006 / V007), whose outputs have one speed. */
    bool setPinSpeed(int pin, CH32RVPinSpeed speed);
    /* Returns only when it cannot sleep: otherwise the chip restarts. */
    CH32RVSleepResult deepSleep();
    /* What ended the last sleep - also across deepSleep()'s restart. */
    CH32RVWakeupCause wakeupCause();
    /* Moves on whenever millis() missed time it could not measure (a pin
     * woke part-way through a sleep). Compare it before and after to know
     * whether a deadline across the sleep can be trusted. */
    uint32_t timeDomain();

    /* arduino-esp32's ESP.* names for the same facts. */
    /* The heap: the RAM between the end of .bss and the reserved stack. */
    uint32_t getHeapSize();
    /* What malloc() can still hand out: never-claimed heap plus freed blocks
     * (fragmented - one allocation of this size may still fail). */
    uint32_t getFreeHeap();
    /* The stack the linker reserves above the heap (the Stack Size menu). */
    uint32_t getStackSize();
    /* Bytes at the reservation's bottom the stack has not reached since
     * setup() (its high-water mark, as FreeRTOS's uxTaskGetStackHighWaterMark
     * in arduino-esp32); 0 unless the Stack Check menu is on. */
    uint32_t getMinFreeStack();
    uint32_t getCpuFreqMHz() { return (uint32_t)(F_CPU / 1000000UL); }
    /* The supply voltage in mV, from the ADC's internal reference (typical
     * value, so within a few percent); 0 where the family has none in the
     * data (CH32M030). Die temperature: analogReadTemp(). */
    uint32_t getVddMillivolts() { return ch32rv_adc_vdd_millivolts(); }
    /* CRC-32 of len bytes (CRC-32/MPEG-2: the CRC block's polynomial
     * 0x04C11DB7, from 0xFFFFFFFF, no reflection, no final XOR - not zlib's).
     * The block where the series has one, the same result in software
     * elsewhere (ch32rv_crc.h). Pass the last result as crc to go on with
     * more data. */
    uint32_t crc32(const void *data, size_t len, uint32_t crc = CH32RV_CRC32_INIT)
    {
        return ch32rv_crc32_update(crc, data, len);
    }
    /* The series this sketch was built for ("CH32V003"). A binary only runs on
     * its own series, so this is the chip it is running on. */
    const char *getChipModel();
    /* Flash the sketch occupies (code, constants and .data's initial values),
     * and what is left of the flash this build is linked for - the selected
     * part's, or the smallest in the series for the ANY entry. */
    uint32_t getSketchSize();
    uint32_t getFreeSketchSpace();
#ifdef CH32RV_ESIG_FLACAP_ADDR
    /* getFlashChipSize() and getUniqueId() exist (every series has the
     * electronic signature today; a sketch tests this, not the address). */
#define CH32RV_HAS_UNIQUE_ID 1
    /* The chip's own flash size in bytes, from its electronic signature
     * (FLACAP) - the silicon's, not the menu's (device-data esig.csv). */
    uint32_t getFlashChipSize();
    /* The chip's unique ID: UNIID1 | UNIID2 << 32 from the electronic
     * signature. Only these 64 of the 96 UID bits are unique - UNIID3 reads
     * 0xffffffff on V003/V006/X035/L103 and the same 0xe339e339 on a V203 and
     * a V307 (device-data errata esig-uniid3-not-unique), so it is left out.
     * Deliberately not called getEfuseMac() as on an ESP32: it is not a MAC
     * address, and the name would invite using it as one. As a number,
     * %016llx prints UNIID2 first; ch32rv, the USB serial number and the OEP
     * unit names write UNIID1's eight digits first. */
    uint64_t getUniqueId();
#endif

#ifdef CH32RV_HAS_VIO18
    /* CH32H41x: the supply of the high-speed pins (VIO18 - PB0-1, PC0-3,
     * PD0-7, PE7-15 and more, ch32rv_vio18.h), so the voltage their highs
     * reach. 1200, 1800, 2500 or 3300 mV; false for anything else. Mind what
     * else the board has on VIO18 before raising it. The VIO18 Supply menu
     * sets it before setup() instead. */
    bool setVio18Millivolts(uint32_t mv) { return ch32rv_vio18_set_mv(mv); }
    /* What software set, or 0 while the XO strap's voltage still applies. */
    uint32_t getVio18Millivolts() { return ch32rv_vio18_mv(); }
#endif

#if defined(CH32RV_RUN_V5F)
    /* CH32H41x with CPU Core = V5F: the V3F as a second core. fn(arg) runs
     * there on `stack` (its size in bytes; the stack grows down from its end)
     * with interrupts on; when it returns the V3F waits for the next. false
     * while the V3F is busy or when called on the V3F. setup1() / loop1() in
     * the sketch start it before setup() instead. What the V3F may use - a
     * peripheral the V5F does not (Serial1 and the like, pins, timers, their
     * interrupts), never the system services (USB, the IP stack) - is in
     * internal/ch32rv_v3f.h. */
#define CH32RV_HAS_V3F 1
    bool startV3F(void (*fn)(void *), void *arg, void *stack, uint32_t size)
    {
        return stack && size >= 64u && ch32rv_v3f_launch(fn, arg, (uint8_t *)stack + size);
    }
    /* Whether the V3F has a function to run (not yet returned). */
    bool isV3FRunning() { return ch32rv_v3f_running() != 0; }
    /* Whether this code runs on the V3F (false on the V5F). */
    bool isOnV3F() { return ch32rv_on_v3f() != 0; }
#endif

#if defined(CH32RV_HAS_HSEM)
    /* CH32H41x: the hardware semaphore between the V5F and the V3F, channels 0 to hsemCount() - 1 (30 and 31
     * are the core's). hsemTryTake() takes a free channel or returns false at once; hsemTake() tries until it has it or
     * timeoutMs has passed (CH32RV_WAIT_FOREVER: no limit), false at once when this core holds it already;
     * hsemRelease() gives back a channel this core holds (false for one it does not: the other core's stay
     * taken). A channel belongs to a core: an interrupt handler cannot take what its own core's loop holds.
     * Details: ch32rv_multicore.h. */
    bool hsemTryTake(uint8_t channel) { return ch32rv_hsem_try_take(channel) != 0; }
    bool hsemTake(uint8_t channel, uint32_t timeoutMs = CH32RV_WAIT_FOREVER)
    {
        return ch32rv_hsem_take(channel, timeoutMs) != 0;
    }
    bool hsemRelease(uint8_t channel) { return ch32rv_hsem_release(channel) != 0; }
    uint8_t hsemCount() { return (uint8_t)CH32RV_HSEM_USER_CHANNELS; }
#endif

#if defined(CH32RV_HAS_IPC)
    /* CH32H41x: one 32-bit message at a time to the other core. ipcSend() waits while the last one has not been
     * taken (up to timeoutMs; 0 from an interrupt handler) and returns whether it went in; onIpcReceive(cb)
     * runs cb(message) in this core's IPC interrupt for each message to it (nullptr: no more); ipcReceive()
     * takes one without a callback. With CPU Core = V3F the V5F stays asleep: all three return false. Details:
     * ch32rv_multicore.h. */
    bool ipcSend(uint32_t message, uint32_t timeoutMs = CH32RV_WAIT_FOREVER)
    {
        return ch32rv_ipc_send(message, timeoutMs) != 0;
    }
    bool ipcReceive(uint32_t &message) { return ch32rv_ipc_receive(&message) != 0; }
    bool onIpcReceive(void (*callback)(uint32_t message)) { return ch32rv_ipc_on_receive(callback) != 0; }
#endif

private:
    uint32_t _latched = 0;     /* RSTSCKR flags, once read */
    bool _read = false;
    bool _panic = false;       /* the core reset after a stack overflow */
    /* wdtEnable()'s setting, put back after a sleep stretched it. */
    bool _wdtOn = false;
    uint8_t _wdtField = 0;
    uint16_t _wdtTicks = 0;
};

}  // namespace arduino

extern arduino::CH32RVSystem CH32RV;
