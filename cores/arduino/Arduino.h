/* Public entry point for sketches. The API surface is ArduinoCore-API
 * (cores/arduino/api, ADR-0009); this header only adds what the CH32 core
 * itself defines: the pin encoding, the generated variant pin map, and the
 * serial instances. */
#pragma once

#include <stdint.h>
#include <stddef.h>

#include "ch32rv_pins.h"
#include "ch32rv_version.h"

#ifdef __cplusplus
#include "api/ArduinoAPI.h"
using namespace arduino;
#else
#include "api/Common.h"
#endif

/* Pad names, per-port validity masks and analog aliases for the selected
 * series (generated; see ADR-0010). */
#include "pins_arduino.h"

/* A product board whose silkscreen prints pin numbers lists, in its variant,
 * the pad behind each number: CH32RV_BOARD_PIN_COUNT and CH32RV_BOARD_PINS
 * { pad for 0, pad for 1, ... }. A number below that count is looked up there;
 * anything else is already a pad (ADR-0010: pads are 64 and up). Every public
 * entry point resolves once and works on pads from then on, so the table costs
 * nothing where a sketch names pads, and a board without one pays nothing. */
#ifdef CH32RV_BOARD_PIN_COUNT
#ifdef __cplusplus
extern "C" {
#endif
extern const uint8_t ch32rv_board_pins[CH32RV_BOARD_PIN_COUNT];
#ifdef __cplusplus
}
#endif
#define CH32RV_PIN_RESOLVE(pin) \
    ((uint32_t)(pin) < CH32RV_BOARD_PIN_COUNT ? ch32rv_board_pins[(pin)] : (pin))
#else
#define CH32RV_PIN_RESOLVE(pin) (pin)
#endif
/* arduino-esp32's name for the same thing (its Nano ESP32 maps D-numbers to
 * GPIOs): the pad behind a pin number. A pad name comes back unchanged. */
#define digitalPinToGPIONumber(pin) CH32RV_PIN_RESOLVE(pin)

/* Pin numbers are sparse, so a range check is not enough: a pin is valid only
 * if its port exists in this series and the port's mask has the bit set. Both
 * fold to a constant when `pin` is a pad name. */
#define ch32rv_pad_is_valid(pad) \
    ((CH32RV_PIN_PORT(pad) < CH32RV_PORT_COUNT) && \
     ((CH32RV_PORT_MASK(CH32RV_PIN_PORT(pad)) >> CH32RV_PIN_BIT(pad)) & 1u))
#define digitalPinIsValid(pin) ch32rv_pad_is_valid(CH32RV_PIN_RESOLVE(pin))

/* Same, restricted to the pads present on every part in the series - the set a
 * sketch built for the ANY menu entry can rely on. */
#define ch32rv_pad_is_common(pad) \
    ((CH32RV_PIN_PORT(pad) < CH32RV_PORT_COUNT) && \
     ((CH32RV_PORT_COMMON_MASK(CH32RV_PIN_PORT(pad)) >> CH32RV_PIN_BIT(pad)) & 1u))
#define digitalPinIsCommon(pin) ch32rv_pad_is_common(CH32RV_PIN_RESOLVE(pin))

/* EXTI lines are numbered by the pin's bit, not by the port, so the pin number
 * carries everything attachInterrupt() needs. */
#define digitalPinToInterrupt(pin) (pin)
/* What AVR / arduino-esp32 return for a pin with no interrupt; libraries compare
 * against it. */
#define NOT_AN_INTERRUPT -1

/* AVR's cycle helpers (arduino-esp32 has them too); DHT and friends time with them. */
#define clockCyclesPerMicrosecond()  (F_CPU / 1000000L)
#define clockCyclesToMicroseconds(a) ((a) / clockCyclesPerMicrosecond())
#define microsecondsToClockCycles(a) ((a) * clockCyclesPerMicrosecond())

/* arduino-esp32's spelling of ArduinoCore-API's OUTPUT_OPENDRAIN. */
#define OUTPUT_OPEN_DRAIN OUTPUT_OPENDRAIN

/* Port access, in the shape the ESP32 core uses: a pointer to a 32-bit
 * register where one bit is one pin. CH32's OUTDR and INDR are exactly that,
 * including on the 24-bit ports of X033/X035, so a library written against
 * these macros gets the same meaning it has there.
 *
 * portModeRegister() is deliberately absent. A CH32 pin's direction is not one
 * bit: it is a four-bit CNF+MODE field spread across CFGLR, CFGHR and CFGXR,
 * and no single pointer can stand for that. Returning CFGLR would compile and
 * then silently do the wrong thing for any pin above bit 7, so this core would
 * rather not compile.
 *
 * These reach the same registers the core uses. Driving a pin this way while
 * Serial, Wire or SPI owns it is the caller's problem to avoid. */
#define digitalPinToPort(pin)    CH32RV_PIN_PORT(CH32RV_PIN_RESOLVE(pin))
#define digitalPinToBitMask(pin) (1UL << CH32RV_PIN_BIT(CH32RV_PIN_RESOLVE(pin)))
#define portOutputRegister(port) \
    ((volatile uint32_t *)(CH32RV_GPIO_PORT_BASE(port) + 0x0Cu))
#define portInputRegister(port) \
    ((volatile uint32_t *)(CH32RV_GPIO_PORT_BASE(port) + 0x08u))

/* Where the global interrupt enable lives for the code a sketch runs.
 *
 * Sketches enter U mode (MPP = 0 in the board's CH32RV_MSTATUS_INIT) on every
 * QingKe V3B/V3F/V3V/V4 part, as WCH's own startups do, and the core enforces
 * it: mstatus is an illegal instruction there (mcause 2). Their CSR 0x800
 * (gintenr) holds the same enable bits (0x88), is user-accessible, and is what
 * WCH's __enable_irq/__disable_irq use on those cores. Measured on V4C - the
 * X035 on 2026-09-22, the L103 on 2026-09-23 - and on V4F, the V307, the same
 * day; V3B/V3F/V3V/V4A/V4B/V4J follow their EVT headers, not measured.
 *
 * The V2 parts and the V3A (CH32V103) run sketches in M mode and use mstatus.
 * The V3A has U mode but no gintenr (it reads 0 and ignores writes), so it is
 * put in M mode by its board (tools/generate/generate.py) rather than left
 * without a way to mask interrupts. The branch is on the core (CH32RV_CORE_*
 * from the generated variant), not on part names. */
#if defined(CH32RV_CORE_QINGKE_V3B) || defined(CH32RV_CORE_QINGKE_V3F) || \
    defined(CH32RV_CORE_QINGKE_V3V) || \
    defined(CH32RV_CORE_QINGKE_V4A) || defined(CH32RV_CORE_QINGKE_V4B) || \
    defined(CH32RV_CORE_QINGKE_V4C) || defined(CH32RV_CORE_QINGKE_V4F) || \
    defined(CH32RV_CORE_QINGKE_V4J)
#define CH32RV_IRQ_IN_GINTENR 1
#elif defined(CH32RV_CORE_QINGKE_V2A) || defined(CH32RV_CORE_QINGKE_V2C) || \
      defined(CH32RV_CORE_QINGKE_V3A)
#define CH32RV_IRQ_IN_GINTENR 0
#else
#error "unknown QingKe core: say whether sketches run in U mode (gintenr) or M mode (mstatus)"
#endif

/* api/Common.h declares these two but leaves them to the core; see above for
 * which CSR they touch.
 *
 * noInterrupts() does not nest: a second call still leaves one interrupts()
 * away from enabled, which is the AVR behaviour libraries are written
 * against. */
static inline void interrupts(void)
{
#if CH32RV_IRQ_IN_GINTENR
    const uint32_t mask = 0x88u;
    __asm__ volatile ("csrs 0x800, %0" :: "r"(mask) : "memory");
#else
    __asm__ volatile ("csrsi mstatus, 8" ::: "memory");
#endif
}

static inline void noInterrupts(void)
{
#if CH32RV_IRQ_IN_GINTENR
    const uint32_t mask = 0x88u;
    __asm__ volatile ("csrc 0x800, %0" :: "r"(mask) : "memory");
#else
    __asm__ volatile ("csrci mstatus, 8" ::: "memory");
#endif
}

/* Save-and-disable / restore pair for short critical sections in the core and
 * its libraries. Same rule as above (mcause 2 in mstatus - seen when Wire's
 * requestFrom() masked interrupts around STOP on the X035, and when analogWrite()
 * took over a timer on the L103 and the V307; each sketch hung in the trap
 * handler). */
static inline uint32_t ch32rv_irq_save(void)
{
    uint32_t old;
#if CH32RV_IRQ_IN_GINTENR
    const uint32_t mask = 0x88u;
    /* One csrrc, not csrr + csrc: as two statements GCC may give old and mask
     * the same register (old is not early-clobber), and csrc then clears every
     * set bit. On a V4F gintenr is a full view of mstatus, so that dropped FS
     * and the next ISR that saved an FP register trapped (mcause 2 on fsw). */
    __asm__ volatile ("csrrc %0, 0x800, %1" : "=r"(old) : "r"(mask) : "memory");
#else
    __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(old) :: "memory");
#endif
    return old;
}

static inline void ch32rv_irq_restore(uint32_t old)
{
#if CH32RV_IRQ_IN_GINTENR
    if (old & 0x88u) {
        const uint32_t mask = 0x88u;
        __asm__ volatile ("csrs 0x800, %0" :: "r"(mask) : "memory");
    }
#else
    if (old & 8u) {
        __asm__ volatile ("csrsi mstatus, 8" ::: "memory");
    }
#endif
}

#ifdef NUM_ANALOG_INPUTS
#define digitalPinToAnalogChannel(pin) CH32RV_PIN_TO_ADC_CHANNEL(CH32RV_PIN_RESOLVE(pin))
#define analogInputToDigitalPin(chan)  CH32RV_ADC_CHANNEL_TO_PIN(chan)
#define digitalPinHasADC(pin) \
    (CH32RV_PIN_TO_ADC_CHANNEL(CH32RV_PIN_RESOLVE(pin)) != NOT_AN_ANALOG_PIN)
#endif

#ifdef __cplusplus
extern "C" {
#endif
void SystemInit(void);

/* ArduinoCore-API does not declare these two - they arrived in the Arduino
 * API after the version this core pins, and several cores define them
 * themselves. Ours are implemented in C (wiring_analog.c / wiring_pwm.c), so
 * the declarations belong in this extern "C" block: without them a sketch
 * cannot call a function the core has had all along. */
void analogReadResolution(int bits);
void analogWriteResolution(int bits);
/* arduino-esp32's: the PWM frequency analogWrite() uses from now on, in Hz
 * (default 1000). A timer is shared by its channels, so the pads on the same
 * timer follow at their next analogWrite(), as LEDC channels on one timer do
 * on an ESP32. Clamped to what the timer can make at 256 steps
 * (F_CPU / 256 at most). The pin is accepted for the signature's sake. */
void analogWriteFrequency(pin_size_t pin, uint32_t frequency);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
/* arduino-esp32 3.x's form of the resolution setter. The resolution here is
 * one setting for every pin, as it is on AVR, so the pin only selects the
 * signature. */
static inline void analogWriteResolution(pin_size_t pin, uint8_t bits)
{
    (void)pin;
    analogWriteResolution((int)bits);
}

/* arduino-esp32's name and argument order for ArduinoCore-API's
 * attachInterruptParam(pin, callback, mode, arg). */
static inline void attachInterruptArg(pin_size_t pin, void (*callback)(void *), void *arg,
                                      int mode)
{
    attachInterruptParam(pin, callback, (PinStatus)mode, arg);
}
#endif

#ifdef __cplusplus
#include "HardwareSerial.h"

void setup(void);
void loop(void);
#endif
