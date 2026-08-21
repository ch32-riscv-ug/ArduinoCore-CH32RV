/* SPI for CH32 RISC-V.
 *
 * Controller (master) mode, polled. The pins come from the variant's
 * CH32_SPIn_SCK/MISO/MOSI; chip select is not among them because Arduino
 * drives it as an ordinary GPIO, which is also what lets one bus carry several
 * devices.
 *
 * Peripheral (slave) mode is not implemented, so SPI_HAS_PERIPHERAL_MODE is
 * deliberately not defined - a library can test for it rather than discover it
 * at runtime.
 */
#pragma once

#include "api/HardwareSPI.h"
#include "ch32_pins.h"
#include "pins_arduino.h"

#include <stdint.h>

/* Legacy divider names. AVR's SPI.setClockDivider() took these, and enough
 * libraries still call it that leaving them out breaks compilation for no
 * gain. The divider is against F_CPU, as it is there. */
#define SPI_CLOCK_DIV2   2
#define SPI_CLOCK_DIV4   4
#define SPI_CLOCK_DIV8   8
#define SPI_CLOCK_DIV16  16
#define SPI_CLOCK_DIV32  32
#define SPI_CLOCK_DIV64  64
#define SPI_CLOCK_DIV128 128

namespace arduino {

class CH32SPIClass : public HardwareSPI {
public:
    CH32SPIClass(uint32_t base, bool on_apb1, uint32_t clock_bit,
                 uint8_t sck_pin, uint8_t miso_pin, uint8_t mosi_pin,
                 uint32_t remap_mask, uint32_t remap_value,
                 uint32_t remap2_mask, uint32_t remap2_value)
        : _base(base), _on_apb1(on_apb1), _clock_bit(clock_bit),
          _sck_pin(sck_pin), _miso_pin(miso_pin), _mosi_pin(mosi_pin),
          _remap_mask(remap_mask), _remap_value(remap_value),
          _remap2_mask(remap2_mask), _remap2_value(remap2_value) {}

    void begin() override;
    void end() override;

    uint8_t transfer(uint8_t data) override;
    uint16_t transfer16(uint16_t data) override;
    void transfer(void *buf, size_t count) override;

    void beginTransaction(SPISettings settings) override;
    void endTransaction(void) override;

    /* Nothing to do: this driver never touches the bus from an interrupt, so
     * there is no shared state for an ISR to walk into. Declared because
     * HardwareSPI requires them, and because a library calling
     * usingInterrupt() must still compile. */
    void usingInterrupt(int interruptNumber) override { (void)interruptNumber; }
    void notUsingInterrupt(int interruptNumber) override { (void)interruptNumber; }
    void attachInterrupt() override {}
    void detachInterrupt() override {}

    /* Pre-transaction API. Still used by a lot of library code. */
    void setBitOrder(BitOrder order);
    void setDataMode(uint8_t mode);
    void setClockDivider(uint32_t divider);

private:
    void apply(uint32_t clock_hz, BitOrder order, uint8_t mode);

    const uint32_t _base;
    const bool _on_apb1;
    const uint32_t _clock_bit;
    const uint8_t _sck_pin;
    const uint8_t _miso_pin;
    const uint8_t _mosi_pin;
    /* AFIO route for these pins, written on every begin() - see
     * HardwareSerial::begin for why the default route is written too. */
    const uint32_t _remap_mask;
    const uint32_t _remap_value;
    const uint32_t _remap2_mask;
    const uint32_t _remap2_value;

    uint32_t _clock_hz = 4000000;
    BitOrder _order = MSBFIRST;
    uint8_t _mode = 0;
    bool _started = false;
};

}  // namespace arduino

/* Bus order, as everywhere else in the Arduino ecosystem: the bare name is the
 * first bus. */
#if defined(CH32_SPI1_SCK)
extern arduino::CH32SPIClass SPI;        /* SPI1 */
#if defined(CH32_SPI2_SCK)
extern arduino::CH32SPIClass SPI1;       /* SPI2 */
#endif
#if defined(CH32_SPI3_SCK)
extern arduino::CH32SPIClass SPI2;       /* SPI3 */
#endif
#elif defined(CH32_SPI2_SCK)
extern arduino::CH32SPIClass SPI;        /* SPI2, on a part with no SPI1 */
#if defined(CH32_SPI3_SCK)
extern arduino::CH32SPIClass SPI1;       /* SPI3 */
#endif
#endif
