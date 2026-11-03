/* SPIClass: the SPI controller type every SPI library takes.
 *
 * Libraries are written against `SPIClass *` / `SPIClass &` (SdFat, TFT and
 * display drivers, Adafruit_BusIO ...), and some forward-declare it as
 * `class SPIClass;`, which only compiles when SPIClass is a class at global
 * scope - so it is one here, not ArduinoCore-API's typedef of HardwareSPI
 * (removed by a vendor patch, tools/vendor/check_api_sync.py).
 *
 * It is the common base of the hardware bus (<SPI.h>: CH32RVSPIClass, which
 * SPI, SPI1 and SPI2 are) and the bit-banged one (<SoftSPI.h>), so a library
 * handed either works unchanged, as TwoWire is for I2C. The base carries what
 * both share - the word and buffer transfers built
 * on transfer() - and leaves the bus itself to the two, including the pin
 * form of begin() and the pre-transaction setters, which each bus has as a
 * plain member (virtual ones would be linked whether used or not). The extras
 * are arduino-esp32's, with its names and argument order:
 *
 *   SPI.begin(PA5, PA6, PA7);           // SCK, MISO, MOSI; all -1 keeps the pins
 *   SPI.transferBytes(out, in, n);
 *   SPI.writeBytes(out, n);
 *   SPI.transfer32(word);
 */
#pragma once

#include "Arduino.h"
#include "api/HardwareSPI.h"

#include <stdint.h>

#define SPI_CLOCK_DIV2   2
#define SPI_CLOCK_DIV4   4
#define SPI_CLOCK_DIV8   8
#define SPI_CLOCK_DIV16  16
#define SPI_CLOCK_DIV32  32
#define SPI_CLOCK_DIV64  64
#define SPI_CLOCK_DIV128 128

class SPIClass : public arduino::HardwareSPI {
public:
    /* Two 8-bit frames in the bit order's direction (MSBFIRST: high byte
     * first, LSBFIRST: low byte first), as AVR's SPI does. */
    uint16_t transfer16(uint16_t data) override;
    void transfer(void *buf, size_t count) override;
    using arduino::HardwareSPI::transfer;
    /* arduino-esp32's buffer and wide-word transfers. A word's bytes go in the
     * bit order's direction, as transfer16() does. */
    uint32_t transfer32(uint32_t data);
    void transferBytes(const uint8_t *data, uint8_t *out, uint32_t size);
    void writeBytes(const uint8_t *data, uint32_t size) { transferBytes(data, nullptr, size); }
    void write(uint8_t data) { (void)transfer(data); }
    void write16(uint16_t data) { (void)transfer16(data); }
    void write32(uint32_t data) { (void)transfer32(data); }

    /* Nothing to do: neither bus touches the wire from an interrupt, so there
     * is no shared state for an ISR to walk into. Declared because HardwareSPI
     * requires them, and because a library calling usingInterrupt() must still
     * compile. */
    void usingInterrupt(int interruptNumber) override { (void)interruptNumber; }
    void notUsingInterrupt(int interruptNumber) override { (void)interruptNumber; }
    void attachInterrupt() override {}
    void detachInterrupt() override {}

protected:
    BitOrder _order = MSBFIRST;
    bool _started = false;
};
