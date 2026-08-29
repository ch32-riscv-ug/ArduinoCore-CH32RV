/* Bit-banged SPI controller for CH32 RISC-V.
 *
 * The same API as <SPI.h>, on any three pads. It exists because the hardware
 * SPI can only reach the pads its routes name, and on the small parts those
 * are often gone - bonded out on no package you have, or already spoken for.
 * This is the way out: slower, but it goes anywhere digitalWrite() goes.
 *
 * It derives from HardwareSPI, so anything written against `SPIClass&` takes
 * one of these unchanged:
 *
 *   SoftSPI bus(PC5, PC6, PC7);      // SCK, MOSI, MISO
 *   Adafruit_Something dev(&bus);
 *
 * Chip select is not here, exactly as it is not in <SPI.h>: Arduino drives it
 * as an ordinary GPIO, which is what lets one bus carry several devices.
 *
 * **The clock is whatever the loop runs at.** SPISettings' frequency is
 * accepted and ignored - see setHalfPeriodUs() below for the one knob that
 * does change the speed. Nothing breaks: SPI is clocked by the controller, so
 * a slow clock only means a slow transfer.
 */
#pragma once

#include "api/HardwareSPI.h"
#include "ch32_pins.h"

#include <stdint.h>

namespace arduino {

class SoftSPI : public HardwareSPI {
public:
    /* miso may be left out for a write-only bus - a display, a shift
     * register, a LED driver. transfer() then returns 0. */
    SoftSPI(uint8_t sck, uint8_t mosi, uint8_t miso = NOT_A_PIN)
        : _sck(sck), _mosi(mosi), _miso(miso) {}

    void begin() override;
    void end() override;

    uint8_t transfer(uint8_t data) override;
    uint16_t transfer16(uint16_t data) override;
    void transfer(void *buf, size_t count) override;

    void beginTransaction(SPISettings settings) override;
    void endTransaction(void) override;

    /* Nothing to do: this bus is driven entirely from the caller's thread, so
     * there is no ISR-shared state. Declared because HardwareSPI requires
     * them, and because a library calling usingInterrupt() must still
     * compile. */
    void usingInterrupt(int interruptNumber) override { (void)interruptNumber; }
    void notUsingInterrupt(int interruptNumber) override { (void)interruptNumber; }
    void attachInterrupt() override {}
    void detachInterrupt() override {}

    /* Half of one clock period, in microseconds. 0 - the default - means "as
     * fast as the loop goes", which is the useful setting almost always.
     *
     * This is the honest form of a clock setting for a bit-banged bus: the
     * value is a floor on the half period, not a frequency anyone can hit.
     * Raise it for long wires, level shifters, or a device that needs a slower
     * clock than the loop produces. delayMicroseconds() is itself a busy loop
     * against micros(), so 1 is already a large step. */
    void setHalfPeriodUs(uint16_t us) { _half_us = us; }

    /* Pre-transaction API. Still used by a lot of library code. The clock
     * divider is accepted and ignored, for the reason above. */
    void setBitOrder(BitOrder order) { _order = order; }
    void setDataMode(uint8_t mode);
    void setClockDivider(uint32_t divider) { (void)divider; }

private:
    void idle_clock(void);

    const uint8_t _sck;
    const uint8_t _mosi;
    const uint8_t _miso;

    BitOrder _order = MSBFIRST;
    /* CPOL: clock idles high. CPHA: sample on the trailing edge. */
    bool _cpol = false;
    bool _cpha = false;
    uint16_t _half_us = 0;
    bool _started = false;
};

}  // namespace arduino

using arduino::SoftSPI;
