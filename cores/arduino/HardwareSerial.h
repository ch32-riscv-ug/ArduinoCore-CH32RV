/* USART-backed HardwareSerial.
 *
 * Transmit is interrupt-driven through a ring buffer so that print() does not
 * block on the wire; receive is interrupt-driven into a second ring buffer.
 * Serial1..5 are defined one per file (HardwareSerial1.cpp ...), so a sketch
 * links only the ports it names; the USARTs they run on are bound in begin().
 */
#pragma once

#include "api/HardwareSerial.h"
#include "ch32rv_pins.h"
#include "ch32rv_printf.h"
#include "ch32rv_ringbuffer.h"
#include "ch32rv_route.h"
/* Not just for the pad names: CH32RV_SERIALn_TX and CH32RV_SERIAL_DEFAULT below
 * come from here, and this header has to work when it is included first.
 * HardwareSerial.cpp includes it before Arduino.h, and when the variant was
 * only reachable through Arduino.h that left SERIAL_PORT_MONITOR undefined for
 * exactly one translation unit - the one holding the printf() bridge, which
 * then compiled to `return 0` and made every stdio write to Serial a silent
 * no-op. */
#include "pins_arduino.h"

#include <stdint.h>

/* 64 bytes each, which is the AVR core's size and fits CH32V003's 2 KB of RAM
 * with room to spare. Raise it per sketch with a build option, e.g.
 *   arduino-cli compile --build-property build.extra_flags=-DCH32RV_SERIAL_RX_BUFFER_SIZE=256
 * or the same line in a sketch's build_opt.h. Every port the sketch uses grows,
 * so the cost is (RX + TX) x the number of ports it names.
 *
 * The ring keeps one slot unusable to tell empty from full, so a size of N
 * holds N-1 bytes. Sizes must be at least 2. */
#ifndef CH32RV_SERIAL_RX_BUFFER_SIZE
#define CH32RV_SERIAL_RX_BUFFER_SIZE 64
#endif
#ifndef CH32RV_SERIAL_TX_BUFFER_SIZE
#define CH32RV_SERIAL_TX_BUFFER_SIZE 64
#endif

#if CH32RV_SERIAL_RX_BUFFER_SIZE < 2 || CH32RV_SERIAL_TX_BUFFER_SIZE < 2
#error "CH32RV_SERIAL_{RX,TX}_BUFFER_SIZE must be at least 2 (one slot is the empty/full marker)"
#endif

namespace arduino {

/* One object is one port. Which USART it drives is decided in begin(), not
 * here: the object holds state only, and that state is all zero at reset, so a
 * port lives in .bss and costs nothing until begin() binds it. Each USART's
 * constants (base, clock, IRQ, remap field, routes) sit in a const table in
 * HardwareSerial.cpp, and a USART set to Disable for the board
 * (CH32RV_USARTn_MODE = 0) is not in that table at all.
 *
 * Provisional rule until names and USARTs are tied together for good: a
 * begin() without pins binds SerialN to USARTn - its fixed pins when the board
 * sets FIX, the variant's default pins otherwise - which is what every sketch
 * already relies on. begin() with pins binds to whichever enabled USART has
 * them as a route. */
class CH32RVHardwareSerial : public HardwareSerial {
public:
    explicit CH32RVHardwareSerial(uint8_t number) : _number(number) {}

    /* A baud the USART cannot reach from F_CPU (outside F_CPU / 65535 ..
     * F_CPU / 16, or 0) leaves the port closed, reopened or not: check
     * `if (!Serial1)` after begin() when the baud comes from elsewhere. */
    void begin(unsigned long baudrate) override { begin(baudrate, SERIAL_8N1); }
    void begin(unsigned long baudrate, uint16_t config) override;
    /* arduino-esp32's form: the pins (RX, TX) named in the same call. -1, -1
     * keeps the pins set by setPins() / setRoute(), or the default ones; a pair
     * that is no enabled USART's route leaves the port closed, as does invert
     * (no USART here inverts its lines). */
    void begin(unsigned long baudrate, uint16_t config, int rxPin, int txPin,
               bool invert = false);
    void end() override;

    int available(void) override;
    /* Room left in the transmit ring. Print declares it with a default of 0,
     * which is safe but useless: a sketch checking it to avoid blocking would
     * conclude the port is permanently full. */
    int availableForWrite(void) override;
    int peek(void) override;
    int read(void) override;
    void flush(void) override;
    size_t write(uint8_t c) override;
    using Print::write;
    /* AVR's HardwareSerial has these: Serial.write(0) picks the byte write. */
    inline size_t write(unsigned long n) { return write((uint8_t)n); }
    inline size_t write(long n) { return write((uint8_t)n); }
    inline size_t write(unsigned int n) { return write((uint8_t)n); }
    inline size_t write(int n) { return write((uint8_t)n); }

    /* arduino-esp32's Print::printf - see ch32rv_printf.h. */
    CH32RV_PRINTF_MEMBER

    operator bool() override { return _slot != 0; }

    /* The USART number this port is bound to, 0 while closed. */
    uint8_t peripheral(void) const;

    /* Choose the pins for the next begin(), or move an open port onto them.
     *
     * setRoute() names a route of USARTn (this port's provisional USART);
     * setPins() names the pins, and any enabled USART whose route they are
     * will do. Both return false and change nothing when no enabled USART has
     * that route, and setPins() refuses a TX and an RX from different routes -
     * the hardware cannot do that. CTS / RTS must stay -1: hardware flow
     * control is not supported. setPins() takes RX first, as on an ESP32. */
    bool setRoute(uint8_t route);
    bool setPins(int rxPin, int txPin, int ctsPin = -1, int rtsPin = -1);

    /* For the shared interrupt handler only. */
    void irq(uint32_t base);
    uint8_t number(void) const { return _number; }

private:
    bool bind(uint8_t index, const ch32rv_route_t &route);
    void start(void);

    const uint8_t _number;
    /* 0 = closed; otherwise 1 + index into the USART table. */
    uint8_t _slot;
    /* Route chosen by setPins() / setRoute() for the next begin(), already
     * looked up, so begin() itself never searches: a sketch that does not name
     * pins links no route table and no search. nullptr = the default. */
    uint8_t _want_index;
    const ch32rv_route_t *_want;
    uint8_t _tx_pin;
    uint8_t _rx_pin;
    uint16_t _config;
    /* BRR, remembered so a pin change can reopen the port exactly as it was. */
    uint16_t _brr;
    CH32RVRingBuffer<CH32RV_SERIAL_RX_BUFFER_SIZE> _rx;
    CH32RVRingBuffer<CH32RV_SERIAL_TX_BUFFER_SIZE> _tx;
};

}  // namespace arduino

/* The variant names the USART whose pins exist on every part in the series. */
#if defined(CH32RV_SERIAL1_TX)
extern arduino::CH32RVHardwareSerial Serial1;
#endif
#if defined(CH32RV_SERIAL2_TX)
extern arduino::CH32RVHardwareSerial Serial2;
#endif
#if defined(CH32RV_SERIAL3_TX)
extern arduino::CH32RVHardwareSerial Serial3;
#endif
#if defined(CH32RV_SERIAL4_TX)
extern arduino::CH32RVHardwareSerial Serial4;
#endif
#if defined(CH32RV_SERIAL5_TX)
extern arduino::CH32RVHardwareSerial Serial5;
#endif

#if !defined(SERIAL_PORT_MONITOR) && defined(CH32RV_SERIAL_DEFAULT)
#if CH32RV_SERIAL_DEFAULT == 1
#define SERIAL_PORT_MONITOR Serial1
#elif CH32RV_SERIAL_DEFAULT == 2
#define SERIAL_PORT_MONITOR Serial2
#elif CH32RV_SERIAL_DEFAULT == 3
#define SERIAL_PORT_MONITOR Serial3
#elif CH32RV_SERIAL_DEFAULT == 4
#define SERIAL_PORT_MONITOR Serial4
#elif CH32RV_SERIAL_DEFAULT == 5
#define SERIAL_PORT_MONITOR Serial5
#endif
#endif

#if defined(SERIAL_PORT_MONITOR) && !defined(Serial)
#define Serial SERIAL_PORT_MONITOR
#endif

/* Retarget printf()/puts()/stderr. The default is the monitor port above.
 *
 *   ch32rv_set_stdout(&SerialSDI);   // trace over the debug probe, no wiring
 *   ch32rv_set_stdout(nullptr);      // discard
 *
 * This moves *stdio* only. The name `Serial` is bound at compile time and does
 * not follow - a sketch that wants to print to SDI writes SerialSDI.println()
 * as usual. Anything deriving from Print works, so a library that provides a
 * USB CDC port plugs in the same way, without the core knowing it exists. */
void ch32rv_set_stdout(arduino::Print *out);
arduino::Print *ch32rv_get_stdout(void);
