/* USART-backed HardwareSerial.
 *
 * Transmit is interrupt-driven through a ring buffer so that print() does not
 * block on the wire; receive is interrupt-driven into a second ring buffer.
 * One instance per USART, wired up in HardwareSerial.cpp from the variant's
 * CH32RV_SERIALn_TX / CH32RV_SERIALn_RX definitions.
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
 * or the same line in a sketch's build_opt.h. Every instance grows, so the cost
 * is (RX + TX) x the number of USARTs the variant defines.
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

class CH32RVHardwareSerial : public HardwareSerial {
public:
    CH32RVHardwareSerial(uint32_t base, uint8_t irqn, uint8_t tx_pin,
                       uint8_t rx_pin, uint32_t clken_addr, uint32_t clken_mask,
                       uint32_t remap_mask, uint32_t remap_value,
                       uint32_t remap2_mask, uint32_t remap2_value)
        : _base(base), _irqn(irqn), _tx_pin(tx_pin), _rx_pin(rx_pin),
          _clken_addr(clken_addr), _clken_mask(clken_mask), _remap_mask(remap_mask),
          _remap_value(remap_value), _remap2_mask(remap2_mask),
          _remap2_value(remap2_value), _started(false) {}

    /* A baud the USART cannot reach from F_CPU (outside F_CPU / 65535 ..
     * F_CPU / 16, or 0) leaves the port closed, reopened or not: check
     * `if (!Serial1)` after begin() when the baud comes from elsewhere. */
    void begin(unsigned long baudrate) override { begin(baudrate, SERIAL_8N1); }
    void begin(unsigned long baudrate, uint16_t config) override;
    /* arduino-esp32's form: the pins (RX, TX) named in the same call. -1, -1
     * keeps the pins the port has (the variant's default, or the last
     * setPins() / setRoute()); a pair that is not one route leaves the port
     * closed, as does invert (no USART here inverts its lines). */
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

    operator bool() override { return _started; }

    /* Move this port onto another of its pin routes.
     *
     * Both forms return false and change nothing when the route does not
     * exist on this series, and setPins() additionally refuses a TX and an RX
     * that belong to different routes - the hardware cannot do that, and
     * silently honouring one of the two is how a sketch ends up transmitting
     * on a pad nobody is watching.
     *
     * Calling either while the port is open reopens it on the new pins with
     * the same baud rate and framing, and returns the pads it left to inputs.
     *
     * setPins() takes RX first, as on an ESP32. CTS / RTS are there for the
     * same reason and must stay -1: hardware flow control is not supported.
     */
    bool setRoute(uint8_t route);
    bool setPins(int rxPin, int txPin, int ctsPin = -1, int rtsPin = -1);

    /* Called from the generated interrupt handler. */
    void irq(void);

private:
    void start_tx(void);

    bool use_route(const ch32rv_route_t &route);

    const uint32_t _base;
    const uint8_t _irqn;
    /* Not const: setRoute()/setPins() move the port between pin sets. */
    uint8_t _tx_pin;
    uint8_t _rx_pin;
    /* RCC enable register and bit, from the variant (clock_enables.csv):
     * which bus a USART hangs off differs per family. */
    const uint32_t _clken_addr;
    const uint32_t _clken_mask;
    /* AFIO field that routes this USART to _tx_pin/_rx_pin; zero mask
     * means device-data knows no field, so the pins cannot be moved. */
    const uint32_t _remap_mask;
    uint32_t _remap_value;
    /* Second half of a field that spans PCFR2. */
    const uint32_t _remap2_mask;
    uint32_t _remap2_value;
    bool _started;
    /* Remembered so a route change can reopen the port exactly as it was. */
    unsigned long _baudrate = 0;
    uint16_t _config = 0;
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
