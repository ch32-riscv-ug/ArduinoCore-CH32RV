/* Public entry point for sketches. The API surface is ArduinoCore-API
 * (cores/arduino/api, ADR-0009); this header only adds what the CH32 core
 * itself defines: the pin encoding, the generated variant pin map, and the
 * serial instances. */
#pragma once

#include <stdint.h>
#include <stddef.h>

#include "ch32_pins.h"

#ifdef __cplusplus
#include "api/ArduinoAPI.h"
using namespace arduino;
#else
#include "api/Common.h"
#endif

/* Pad names, per-port validity masks and analog aliases for the selected
 * series (generated; see ADR-0010). */
#include "pins_arduino.h"

/* Pin numbers are sparse, so a range check is not enough: a pin is valid only
 * if its port exists in this series and the port's mask has the bit set. Both
 * fold to a constant when `pin` is a pad name. */
#define digitalPinIsValid(pin) \
    ((CH32_PIN_PORT(pin) < CH32_PORT_COUNT) && \
     ((CH32_PORT_MASK(CH32_PIN_PORT(pin)) >> CH32_PIN_BIT(pin)) & 1u))

/* Same, restricted to the pads present on every part in the series - the set a
 * sketch built for the ANY menu entry can rely on. */
#define digitalPinIsCommon(pin) \
    ((CH32_PIN_PORT(pin) < CH32_PORT_COUNT) && \
     ((CH32_PORT_COMMON_MASK(CH32_PIN_PORT(pin)) >> CH32_PIN_BIT(pin)) & 1u))

/* EXTI lines are numbered by the pin's bit, not by the port, so the pin number
 * carries everything attachInterrupt() needs. */
#define digitalPinToInterrupt(pin) (pin)

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
#define digitalPinToPort(pin)    CH32_PIN_PORT(pin)
#define digitalPinToBitMask(pin) (1UL << CH32_PIN_BIT(pin))
#define portOutputRegister(port) \
    ((volatile uint32_t *)(CH32_GPIO_PORT_BASE(port) + 0x0Cu))
#define portInputRegister(port) \
    ((volatile uint32_t *)(CH32_GPIO_PORT_BASE(port) + 0x08u))

/* Where the global interrupt enable lives for the code a sketch runs.
 *
 * Sketches enter U mode (MPP = 0 in the board's CH32_MSTATUS_INIT) on every
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
 * without a way to mask interrupts. The branch is on the core (CH32_CORE_*
 * from the generated variant), not on part names. */
#if defined(CH32_CORE_QINGKE_V3B) || defined(CH32_CORE_QINGKE_V3F) || \
    defined(CH32_CORE_QINGKE_V3V) || \
    defined(CH32_CORE_QINGKE_V4A) || defined(CH32_CORE_QINGKE_V4B) || \
    defined(CH32_CORE_QINGKE_V4C) || defined(CH32_CORE_QINGKE_V4F) || \
    defined(CH32_CORE_QINGKE_V4J)
#define CH32_IRQ_IN_GINTENR 1
#elif defined(CH32_CORE_QINGKE_V2A) || defined(CH32_CORE_QINGKE_V2C) || \
      defined(CH32_CORE_QINGKE_V3A)
#define CH32_IRQ_IN_GINTENR 0
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
#if CH32_IRQ_IN_GINTENR
    const uint32_t mask = 0x88u;
    __asm__ volatile ("csrs 0x800, %0" :: "r"(mask) : "memory");
#else
    __asm__ volatile ("csrsi mstatus, 8" ::: "memory");
#endif
}

static inline void noInterrupts(void)
{
#if CH32_IRQ_IN_GINTENR
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
static inline uint32_t ch32_irq_save(void)
{
    uint32_t old;
#if CH32_IRQ_IN_GINTENR
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

static inline void ch32_irq_restore(uint32_t old)
{
#if CH32_IRQ_IN_GINTENR
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
#define digitalPinToAnalogChannel(pin) CH32_PIN_TO_ADC_CHANNEL(pin)
#define analogInputToDigitalPin(chan)  CH32_ADC_CHANNEL_TO_PIN(chan)
#define digitalPinHasADC(pin) \
    (CH32_PIN_TO_ADC_CHANNEL(pin) != NOT_AN_ANALOG_PIN)
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
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include "HardwareSerial.h"

void setup(void);
void loop(void);
#endif
