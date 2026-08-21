/* USART-backed HardwareSerial.
 *
 * Transmit is interrupt-driven through a ring buffer so that print() does not
 * block on the wire; receive is interrupt-driven into a second ring buffer.
 * One instance per USART, wired up in HardwareSerial.cpp from the variant's
 * CH32_SERIALn_TX / CH32_SERIALn_RX definitions.
 */
#pragma once

#include "api/HardwareSerial.h"
#include "ch32_pins.h"
#include "ch32_ringbuffer.h"
#include "ch32_route.h"
/* Not just for the pad names: CH32_SERIALn_TX and CH32_SERIAL_DEFAULT below
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
 *   arduino-cli compile --build-property build.extra_flags=-DCH32_SERIAL_RX_BUFFER_SIZE=256
 * or the same line in a sketch's build_opt.h. Every instance grows, so the cost
 * is (RX + TX) x the number of USARTs the variant defines.
 *
 * The ring keeps one slot unusable to tell empty from full, so a size of N
 * holds N-1 bytes. Sizes must be at least 2. */
#ifndef CH32_SERIAL_RX_BUFFER_SIZE
#define CH32_SERIAL_RX_BUFFER_SIZE 64
#endif
#ifndef CH32_SERIAL_TX_BUFFER_SIZE
#define CH32_SERIAL_TX_BUFFER_SIZE 64
#endif

#if CH32_SERIAL_RX_BUFFER_SIZE < 2 || CH32_SERIAL_TX_BUFFER_SIZE < 2
#error "CH32_SERIAL_{RX,TX}_BUFFER_SIZE must be at least 2 (one slot is the empty/full marker)"
#endif

namespace arduino {

class CH32HardwareSerial : public HardwareSerial {
public:
    CH32HardwareSerial(uint32_t base, uint8_t irqn, uint8_t tx_pin,
                       uint8_t rx_pin, bool on_apb1, uint32_t clock_bit,
                       uint32_t remap_mask, uint32_t remap_value,
                       uint32_t remap2_mask, uint32_t remap2_value)
        : _base(base), _irqn(irqn), _tx_pin(tx_pin), _rx_pin(rx_pin),
          _on_apb1(on_apb1), _clock_bit(clock_bit), _remap_mask(remap_mask),
          _remap_value(remap_value), _remap2_mask(remap2_mask),
          _remap2_value(remap2_value), _started(false) {}

    void begin(unsigned long baudrate) override { begin(baudrate, SERIAL_8N1); }
    void begin(unsigned long baudrate, uint16_t config) override;
    void end() override;

    int available(void) override;
    int peek(void) override;
    int read(void) override;
    void flush(void) override;
    size_t write(uint8_t c) override;
    using Print::write;

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
     */
    bool setRoute(uint8_t route);
    bool setPins(uint8_t tx, uint8_t rx);

    /* Called from the generated interrupt handler. */
    void irq(void);

private:
    void start_tx(void);

    bool use_route(const ch32_route_t &route);

    const uint32_t _base;
    const uint8_t _irqn;
    /* Not const: setRoute()/setPins() move the port between pin sets. */
    uint8_t _tx_pin;
    uint8_t _rx_pin;
    const bool _on_apb1;
    const uint32_t _clock_bit;
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
    CH32RingBuffer<CH32_SERIAL_RX_BUFFER_SIZE> _rx;
    CH32RingBuffer<CH32_SERIAL_TX_BUFFER_SIZE> _tx;
};

}  // namespace arduino

/* The variant names the USART whose pins exist on every part in the series. */
#if defined(CH32_SERIAL1_TX)
extern arduino::CH32HardwareSerial Serial1;
#endif
#if defined(CH32_SERIAL2_TX)
extern arduino::CH32HardwareSerial Serial2;
#endif
#if defined(CH32_SERIAL3_TX)
extern arduino::CH32HardwareSerial Serial3;
#endif
#if defined(CH32_SERIAL4_TX)
extern arduino::CH32HardwareSerial Serial4;
#endif
#if defined(CH32_SERIAL5_TX)
extern arduino::CH32HardwareSerial Serial5;
#endif

#if !defined(SERIAL_PORT_MONITOR) && defined(CH32_SERIAL_DEFAULT)
#if CH32_SERIAL_DEFAULT == 1
#define SERIAL_PORT_MONITOR Serial1
#elif CH32_SERIAL_DEFAULT == 2
#define SERIAL_PORT_MONITOR Serial2
#elif CH32_SERIAL_DEFAULT == 3
#define SERIAL_PORT_MONITOR Serial3
#elif CH32_SERIAL_DEFAULT == 4
#define SERIAL_PORT_MONITOR Serial4
#elif CH32_SERIAL_DEFAULT == 5
#define SERIAL_PORT_MONITOR Serial5
#endif
#endif

#if defined(SERIAL_PORT_MONITOR) && !defined(Serial)
#define Serial SERIAL_PORT_MONITOR
#endif
