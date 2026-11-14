/* Bit-banged SPI controller for CH32 RISC-V.
 *
 * The same API as <SPI.h>, on any three pads. It exists because the hardware
 * SPI can only reach the pads its routes name, and on the small parts those
 * are often gone - bonded out on no package you have, or already spoken for.
 * This is the way out: slower, but it goes anywhere digitalWrite() goes.
 *
 * It derives from SPIClass (<SPIClass.h>), so anything written against
 * `SPIClass&` takes
 * one of these unchanged:
 *
 *   SoftSPI bus(PC5, PC7, PC6);      // SCK, MISO, MOSI - SPI.begin()'s order
 *
 * or name the pins later, as on an ESP32's SPI: bus.begin(sck, miso, mosi).
 *   Adafruit_Something dev(&bus);
 *
 * Chip select is not here, exactly as it is not in <SPI.h>: Arduino drives it
 * as an ordinary GPIO, which is what lets one bus carry several devices.
 *
 * **The clock is whatever the loop runs at.** SPISettings' frequency is
 * accepted and ignored - see setHalfPeriodUs() below for the one knob that
 * does change the speed. Nothing breaks: SPI is clocked by the controller, so
 * a slow clock only means a slow transfer.
 *
 * A peripheral too, as <SPI.h>'s buses are (SPI_HAS_PERIPHERAL_MODE): a
 * transaction begun with SPISettings(..., SPI_PERIPHERAL) makes SCK and MOSI
 * inputs and MISO an output, and each transfer() then follows the
 * controller's clock by polling SCK, as long as that takes, until
 * endTransaction(). Interrupts are left on, so an interrupt longer than half a
 * clock period loses a bit; SoftSPISlave (<SoftSPISlave.h>) is the slave framed
 * by a chip select and moved by interrupts.
 */
#pragma once

#include "SPIClass.h"
#include "internal/ch32rv_pins.h"

#include <stdint.h>

namespace arduino {

class SoftSPI : public ::SPIClass {
public:
    /* SCK, MISO, MOSI: the order of SPI.begin(sck, miso, mosi) here and on an
     * ESP32. miso may be NOT_A_PIN for a write-only bus - a display, a shift
     * register, a LED driver; transfer() then returns 0. */
    SoftSPI(uint8_t sck, uint8_t miso, uint8_t mosi)
        : _sck(sck), _mosi(mosi), _miso(miso) {}

    void begin() override;
    /* arduino-esp32's form: the pins (SCK, MISO, MOSI) named in the call.
     * All -1 keeps the pins the bus has; false leaves the bus closed. ss is
     * accepted for the same reason and not used: chip select stays a GPIO the
     * sketch (or the library) drives. */
    bool begin(int sck, int miso = -1, int mosi = -1, int ss = -1)
    {
        (void)ss;
        if (_started) {
            end();
        }
        if (!(sck < 0 && miso < 0 && mosi < 0) && !setPins(sck, miso, mosi)) {
            return false;
        }
        begin();
        return true;
    }
    /* sck and mosi must be pins, miso may be -1 (write-only). An open bus
     * reopens on them. */
    bool setPins(int sck, int miso, int mosi);
    void end() override;

    uint8_t transfer(uint8_t data) override;
    using ::SPIClass::transfer;

    /* SPI_PERIPHERAL in the settings: a peripheral until endTransaction(). */
    void beginTransaction(SPISettings settings) override;
    void endTransaction(void) override;

    /* The hardware SPI this bus is bound to: none, as SPI's says while closed. */
    uint8_t peripheral(void) const { return 0; }

    /* Half of one clock period, in microseconds. 0 - the default - means "as
     * fast as the loop goes", which is the useful setting almost always.
     *
     * This is the honest form of a clock setting for a bit-banged bus: the
     * value is a floor on the half period, not a frequency anyone can hit.
     * Raise it for long wires, level shifters, or a device that needs a slower
     * clock than the loop produces. delayMicroseconds() is itself a busy loop
     * against micros(), so 1 is already a large step. */
    void setHalfPeriodUs(uint16_t us) { _half_us = us; }

    /* Pre-transaction API. The clock divider is accepted and ignored, for
     * the reason above. */
    void setBitOrder(BitOrder order) { _order = order; }
    void setDataMode(uint8_t mode);
    void setClockDivider(uint32_t divider) { (void)divider; }

    /* Peripheral mode's two halves, called through ch32rv_spi_peripheral
     * (SPIClass.h); not part of the sketch-facing API. */
    void peripheral_mode(bool on);
    uint8_t follow(uint8_t data);

private:
    bool cpol(void) const { return (_mode & 2u) != 0; }
    bool cpha(void) const { return (_mode & 1u) != 0; }
    void idle_clock(void);
    void controller_pads(void);

    uint8_t _sck;
    uint8_t _mosi;
    uint8_t _miso;

    /* SPI mode: bit 1 CPOL (clock idles high), bit 0 CPHA (sample on the
     * trailing edge). One byte with the flag below, so the object stays 16. */
    uint8_t _mode = 0;
    /* A peripheral (SPI_PERIPHERAL) until endTransaction(). */
    bool _periph = false;
    uint16_t _half_us = 0;
};

}  // namespace arduino

using arduino::SoftSPI;
