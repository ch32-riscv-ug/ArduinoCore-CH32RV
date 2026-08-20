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
/* Not just for the pad names: CH32_SERIALn_TX and CH32_SERIAL_DEFAULT below
 * come from here, and this header has to work when it is included first.
 * HardwareSerial.cpp includes it before Arduino.h, and when the variant was
 * only reachable through Arduino.h that left SERIAL_PORT_MONITOR undefined for
 * exactly one translation unit - the one holding the printf() bridge, which
 * then compiled to `return 0` and made every stdio write to Serial a silent
 * no-op. */
#include "pins_arduino.h"

#include <stdint.h>

namespace arduino {

class CH32HardwareSerial : public HardwareSerial {
public:
    CH32HardwareSerial(uint32_t base, uint8_t irqn, uint8_t tx_pin,
                       uint8_t rx_pin, bool on_apb1, uint32_t clock_bit,
                       uint32_t remap_mask, uint32_t remap_value)
        : _base(base), _irqn(irqn), _tx_pin(tx_pin), _rx_pin(rx_pin),
          _on_apb1(on_apb1), _clock_bit(clock_bit), _remap_mask(remap_mask),
          _remap_value(remap_value), _started(false) {}

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

    /* Called from the generated interrupt handler. */
    void irq(void);

private:
    void start_tx(void);

    const uint32_t _base;
    const uint8_t _irqn;
    const uint8_t _tx_pin;
    const uint8_t _rx_pin;
    const bool _on_apb1;
    const uint32_t _clock_bit;
    /* AFIO PCFR1 field that routes this USART to _tx_pin/_rx_pin; zero mask
     * means the pins are the reset-default route. */
    const uint32_t _remap_mask;
    const uint32_t _remap_value;
    bool _started;
    /* TODO(docs/todo.ja.md): 64 bytes each is not configurable yet. */
    CH32RingBuffer<64> _rx;
    CH32RingBuffer<64> _tx;
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
