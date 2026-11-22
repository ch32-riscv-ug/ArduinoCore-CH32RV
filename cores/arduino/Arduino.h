/* Public entry point for sketches. The API surface is ArduinoCore-API
 * (cores/arduino/api, ADR-0009); this header only adds what the CH32 core
 * itself defines: the pin encoding, the generated variant pin map, and the
 * serial instances. */
#pragma once

#include <stdint.h>
#include <stddef.h>

#include "internal/ch32rv_pins.h"
#include "ch32rv_version.h"
#include "internal/ch32rv_bus_clock.h"   /* CH32RV_HCLK_HZ, CH32RV_PCLK1_HZ / 2_HZ, CH32RV_ADC_CLOCK_OK */

#if !CH32RV_ADC_CLOCK_OK
/* The ADC cannot run at this CPU Clock (internal/ch32rv_bus_clock.h; the
 * entry says "no ADC"): a call that survives the build is an error, rather
 * than a sketch that waits for a conversion forever. The core's own paths
 * read 0 (VDD unknown, analogReadTemp() NAN). Here, ahead of
 * ArduinoCore-API's declaration of analogRead(): clang takes the attribute
 * only on a function's first declaration. */
#ifdef __cplusplus
extern "C" {
#endif
#define CH32RV_ADC_UNAVAILABLE \
    __attribute__((error("the ADC does not run at this CPU Clock (its clock would be below the datasheet's " \
                         "minimum): select a faster Tools > CPU Clock")))
#ifdef EXTENDED_PIN_MODE
int analogRead(uint32_t pinNumber) CH32RV_ADC_UNAVAILABLE;
int analogReadCalibrated(uint32_t pin) CH32RV_ADC_UNAVAILABLE;
uint32_t analogReadMilliVolts(uint32_t pin) CH32RV_ADC_UNAVAILABLE;
#else
int analogRead(uint8_t pinNumber) CH32RV_ADC_UNAVAILABLE;
int analogReadCalibrated(uint8_t pin) CH32RV_ADC_UNAVAILABLE;
uint32_t analogReadMilliVolts(uint8_t pin) CH32RV_ADC_UNAVAILABLE;
#endif
int analogReadVref(void) CH32RV_ADC_UNAVAILABLE;
float analogReadTemp(void) CH32RV_ADC_UNAVAILABLE;
uint16_t touchRead(uint8_t pin) CH32RV_ADC_UNAVAILABLE;
uint32_t ch32rv_adc_vdd_millivolts(void) CH32RV_ADC_UNAVAILABLE;
#ifdef __cplusplus
}
#endif
#endif

#ifdef __cplusplus
/* std::abort / std::malloc and the like. libstdc++'s <string> brings
 * <cstdlib> in; libc++'s does not, and libraries written against GCC cores
 * call std::abort with only <string> (LovyanGFX's result.hpp). The same for
 * <initializer_list>, which `for (auto x : {1, 2})` needs and GCC cores
 * bring in through Arduino.h. */
#include <cstdlib>
#include <initializer_list>
#include "api/ArduinoAPI.h"
using namespace arduino;
#else
#include "api/Common.h"
#endif
/* itoa / ltoa / utoa / ultoa, which the Arduino API declares and libraries
 * call without an include of their own (AVR has them in stdlib.h, ESP32 in
 * stdlib_noniso.h). picolibc has the int forms; itoa.c the long ones. */
#include "api/itoa.h"

/* Background work for the USB device stack. CH32RVUsbDevice defines it and
 * attaches it as a system service in begin() (ch32rv_service.h), so delay()
 * and the return of loop() run it; the library's own blocking calls also call
 * it directly. Weak, so a sketch without USB carries neither. */
#ifdef __cplusplus
extern "C" {
#endif
extern void ch32rv_usb_task(void) __attribute__((weak));
/* The same for the USB host stack (CH32RVUsbHost). */
extern void ch32rv_usb_host_task(void) __attribute__((weak));
/* The same for the IP stack (the Network library): its timers and the frames
 * its link drivers queued. */
extern void ch32rv_net_task(void) __attribute__((weak));
#ifdef __cplusplus
}
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

/* Run a function from RAM: `CH32RV_RAMFUNC void f(void) { ... }`. It costs
 * its size in RAM. No flash wait states, and it keeps running while the flash
 * is erased or programmed - but a call out of it into flash code (the runtime library, an
 * interrupt handler) stops there as usual. On CH32H41x everything already runs
 * from RAM. */
#define CH32RV_RAMFUNC __attribute__((section(".highcode"), noinline))

/* CH32RV_DMA_MEM: a DMA buffer kept out of CH32H41x's V5F DTCM. */
#include "ch32rv_dma_mem.h"

/* arduino-esp32's clock getters. The clock is the board menu's (CPU Clock) and
 * fixed for the build, so these are F_CPU; there is no setter. */
static inline uint32_t getCpuFrequencyMhz(void) { return (uint32_t)(F_CPU / 1000000UL); }
static inline uint32_t getApbFrequency(void) { return (uint32_t)CH32RV_PCLK1_HZ; }

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

/* Where the global interrupt enable lives (gintenr or mstatus, by core) and
 * the save / restore pair: ch32rv_irq.h, which CH32RVSyncKit uses too. */
#include "internal/ch32rv_irq.h"
#include "internal/ch32rv_interrupt.h"

/* deliverEvents(): where the libraries' notification callbacks (onRx and the
 * like) run - after every loop() and where a sketch calls it, never inside
 * delay() (ch32rv_deliver.h). */
#include "internal/ch32rv_deliver.h"

/* timerBegin() / timerAlarm() and the rest of arduino-esp32's timer API,
 * declared as the ESP32 core does without an include of the sketch's. */
#include "CH32RVHwTimer.h"

/* api/Common.h declares these two but leaves them to the core.
 *
 * noInterrupts() does not nest: a second call still leaves one interrupts()
 * away from enabled, which is the AVR behaviour libraries are written
 * against. ch32rv_irq_save() / ch32rv_irq_restore() nest. */
static inline void interrupts(void)
{
    ch32rv_irq_enable_all();
}

static inline void noInterrupts(void)
{
    ch32rv_irq_disable_all();
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
/* The ADC's internal channels (arduino-pico's names). analogReadVref(): the
 * internal reference as analogRead() would return it (uncorrected) - 0 where the family
 * has none in the data (CH32M030). analogReadTemp(): the die temperature in
 * degrees C from the factory's calibration point and the datasheet's slope,
 * NAN where there is no sensor (V00x, X033/X035, X305/X315, M030). */
int analogReadVref(void);
/* analogRead() is the converter's own value, as arduino-esp32's. This one
 * adds the correction the family's vendor code applies before using the ADC
 * (CH32L103 / M103: the EVT's VDDA / 2 offset, which otherwise reads about 620
 * counts low); elsewhere it is analogRead(). Not a millivolt reading: the ADC's
 * reference is VDD, which the core does not know (CH32RV.getVddMillivolts()
 * estimates it from Vrefint, a few percent either way). */
int analogReadCalibrated(pin_size_t pin);
float analogReadTemp(void);
/* arduino-esp32's: the pad's voltage in mV - the reading (as
 * analogReadCalibrated()) scaled by the supply measured against Vrefint just
 * before it, on every call: four conversions where analogRead() does one. Vrefint's typical is the reference, so
 * the answer is within that reference's spread plus the converter's error,
 * a few percent (not measured: libraries/CH32RV/README). 3300 mV is taken as
 * the supply where Vrefint is unknown (CH32M030). 0 for a pad without ADC. */
uint32_t analogReadMilliVolts(pin_size_t pin);
/* arduino-esp32's: the TouchKey's raw count on an analog pin. It moves with
 * the pin's capacitance, so a finger on a pad changes it; an unconnected pad
 * reads near mid-scale. 0 where the family or the pin has no TouchKey (so far
 * V005-V007 / M007, X033 / X035, V20x, V30x). It borrows ADC1 for one
 * conversion, like analogRead(). touchSetCycles(): the charging time, in ADC
 * clocks (0: the family's default); a bigger pad wants a longer one. `sleep`
 * is ESP32's and unused here. */
uint16_t touchRead(uint8_t pin);
void touchSetCycles(uint16_t measure, uint16_t sleep);
/* The supply (VDDA) in mV, measured against Vrefint; 0 where unknown.
 * CH32RV.getVddMillivolts() is the same thing. */
uint32_t ch32rv_adc_vdd_millivolts(void);
/* arduino-esp32's ADC continuous mode, on ADC1 (ch32rv_adc_continuous.c):
 * analogContinuous() sets up the pins (up to 16 ADC1 inputs), a frame of
 * conversions_per_pin rounds and the pace - sampling_freq_hz conversions a
 * second over all the pins, one round of them per tick of a timer the core
 * takes (CH32RVTimer.h; false when none is free) - and the function called
 * from the interrupt as each frame fills (or NULL).
 * analogContinuousStart() / Stop() run and pause it; analogContinuousRead()
 * waits up to timeout_ms for a frame and gives each pin's average over the
 * newest one (raw at analogContinuousSetWidth()'s width, the ADC's own by
 * default, and in mV against the supply measured at analogContinuous());
 * analogContinuousDeinit() frees the timer and gives the ADC back to
 * analogRead(), which must not be used in between. The ADC has no
 * attenuator: analogContinuousSetAtten() is accepted and does nothing. For
 * the samples themselves, a trigger on a pad's edge, or a free-running ADC,
 * see CH32RVAdcStream. */
typedef struct {
    uint8_t pin;          /* ADC pin */
    uint8_t channel;      /* ADC channel */
    int avg_read_raw;     /* ADC average raw data */
    int avg_read_mvolts;  /* ADC average voltage in mV */
} adc_continuous_result_t;
typedef enum {
    ADC_0db,
    ADC_2_5db,
    ADC_6db,
    ADC_11db,
    ADC_ATTENDB_MAX
} adc_attenuation_t;
bool analogContinuous(const uint8_t pins[], size_t pins_count, uint32_t conversions_per_pin,
                      uint32_t sampling_freq_hz, void (*userFunc)(void));
bool analogContinuousRead(adc_continuous_result_t **buffer, uint32_t timeout_ms);
bool analogContinuousStart(void);
bool analogContinuousStop(void);
bool analogContinuousDeinit(void);
void analogContinuousSetAtten(adc_attenuation_t attenuation);
void analogContinuousSetWidth(uint8_t bits);
/* arduino-esp32's: the PWM frequency analogWrite() uses from now on, in Hz
 * (default 1000). A timer is shared by its channels, so the pads on the same
 * timer follow at their next analogWrite(), as LEDC channels on one timer do
 * on an ESP32. Clamped to what the timer can make at 256 steps
 * (F_CPU / 256 at most). The pin is accepted for the signature's sake. */
void analogWriteFrequency(pin_size_t pin, uint32_t frequency);

/* Random words: arduino-esp32's esp_random() / esp_fill_random() under this
 * core's names. From the hardware generator where the series has one
 * (CH32RV_RNG_BASE: V30x, V4x7, H41x); elsewhere - and should the generator
 * fail - a software mix of the unique ID and the time of the call, which is
 * NOT cryptographic (ch32rv_random.c). random() stays Arduino's repeatable
 * sequence; randomSeed(ch32rv_random()) makes it differ from run to run. */
uint32_t ch32rv_random(void);
void ch32rv_fill_random(void *buf, size_t len);

/* arduino-esp32's RGB LED output: one WS2812 / SK6812-type LED (24 bits, its
 * colour order) on any pad, in software with interrupts off for the 30 us of
 * the frame (ch32rv_rgb_led.c) - or, on the pads of the WS2812 block (ARGB:
 * V4x7's PA15, X3x5's PA15 / PD3 / PD4; ch32rv_rgb_led_is_hardware()), by the
 * block and its DMA with interrupts on, as ESP32 sends it by its RMT. Either
 * way it returns once the last bit is out. It does not wait for the LED to latch: the
 * line has to stay low for the LED's reset time (50 us on WS2812, 280 us on
 * WS2812B-V5 and the like) before the colour shows. For a chain,
 * ch32rv_rgb_led_write() below. */
typedef enum {
    LED_COLOR_ORDER_RGB,
    LED_COLOR_ORDER_BGR,
    LED_COLOR_ORDER_BRG,
    LED_COLOR_ORDER_RBG,
    LED_COLOR_ORDER_GBR,
    LED_COLOR_ORDER_GRB,
} rgb_led_color_order_t;
void rgbLedWriteOrdered(pin_size_t pin, rgb_led_color_order_t order, uint8_t red_val, uint8_t green_val,
                        uint8_t blue_val);
/* GRB, the WS2812B's order. */
void rgbLedWrite(pin_size_t pin, uint8_t red_val, uint8_t green_val, uint8_t blue_val);
/* Several LEDs in one frame: `len` bytes in the LEDs' own order (GRB, GRB,
 * ... for WS2812B; four a LED for RGBW parts), with interrupts off for all
 * of it (30 us an LED) and no gap between LEDs - a chain of rgbLedWrite()
 * calls leaves the line low between them for the call itself, which at low
 * clocks is past the LEDs' latch (13 us on V006 at 24 MHz). Not ESP32's:
 * arduino-esp32 has no chain call (Adafruit_NeoPixel's show() is this).
 * On a pad of the WS2812 block the frame goes by DMA with interrupts on,
 * up to 65535 bytes (longer ones by the CPU). */
void ch32rv_rgb_led_write(pin_size_t pin, const uint8_t *data, size_t len);
/* Whether the two calls above send on `pin` by the WS2812 block, leaving
 * interrupts on (the variant's CH32RV_ARGB_PIN_TO_AF), or by the CPU with
 * them off. A constant where `pin` is. */
static inline bool ch32rv_rgb_led_is_hardware(pin_size_t pin)
{
#if CH32RV_HAS_ARGB
    return CH32RV_ARGB_PIN_TO_AF(CH32RV_PIN_RESOLVE(pin)) >= 0;
#else
    (void)pin;
    return false;
#endif
}
/* The name before arduino-esp32 3.0, deprecated there as here. */
__attribute__((deprecated("Use rgbLedWrite() instead.")))
static inline void neopixelWrite(pin_size_t pin, uint8_t red_val, uint8_t green_val, uint8_t blue_val)
{
    rgbLedWrite(pin, red_val, green_val, blue_val);
}
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

/* ---- analogWrite() of a pin known at compile time ----
 * A sketch pays only for what its pins need. analogWrite() on a pad with a
 * default PWM (CH32RV_PWM_PIN_TO_*) goes straight to that timer channel
 * (ch32rv_pwm_default, wiring_pwm.c); the pad table, the remap routes and the
 * complementary outputs (wiring_pwm_any.c) are linked only where a pin needs
 * them or is chosen at run time. Same results either way. A function-like
 * macro, so &analogWrite is still the function. */
#ifdef __cplusplus
extern "C" {
#endif
void ch32rv_pwm_default(pin_size_t pin, int value);
#ifdef __cplusplus
}
#endif

#ifdef CH32RV_PWM_PIN_COUNT
#define CH32RV_PIN_DEFAULT_PWM(pad) (CH32RV_PWM_PIN_TO_TIMER(pad) != 0)
#else
#define CH32RV_PIN_DEFAULT_PWM(pad) 0
#endif

static inline __attribute__((always_inline)) void ch32rv_analog_write(pin_size_t pin, int value)
{
    if (__builtin_constant_p(CH32RV_PIN_RESOLVE(pin)) && CH32RV_PIN_DEFAULT_PWM(CH32RV_PIN_RESOLVE(pin))) {
        ch32rv_pwm_default(pin, value);
    } else {
        (analogWrite)(pin, value);
    }
}
#define analogWrite(pin, value) ch32rv_analog_write((pin), (value))
