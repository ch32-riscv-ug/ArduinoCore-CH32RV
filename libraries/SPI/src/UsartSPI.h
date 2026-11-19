/* An SPI controller on a USART: the USART's synchronous mode
 * (HardwareSerial::beginSynchronous()), its CK pad the clock, TX the data out
 * (MOSI) and RX the data in (MISO) - AVR's "USART in SPI mode" (MSPIM) on the
 * CH32. A third or fourth SPI bus where the SPI peripherals are taken or their
 * pads are not on the package, at the USART's speed.
 *
 * It derives from SPIClass (<SPIClass.h>), so anything written against
 * `SPIClass &` takes one unchanged:
 *
 *   UsartSPI bus(Serial1);           // Serial1's USART on its route: CK, RX, TX
 *   bus.begin();
 *   Adafruit_Something dev(&bus);
 *
 * or name the pads as SPI.begin() does: bus.begin(sck, miso, mosi) - MISO and
 * MOSI pick Serial1's route (its RX and TX), and SCK must be that route's CK.
 *
 * What the USART does not have: a peripheral mode (SPI_PERIPHERAL is
 * refused - begin() fails and transfers return 0), clocks above the USART's
 * clock / 16, and MSBFIRST in hardware (the bits are reversed here, which
 * costs a few cycles a byte). The port is the Serial object's: while the bus
 * is open, that Serial is not a UART; after end(), Serial.begin() takes it back.
 * Chip select is an ordinary GPIO, as on <SPI.h>.
 */
#pragma once

#include "SPIClass.h"
#include "HardwareSerial.h"

#include <stdint.h>

namespace arduino {

class UsartSPI : public ::SPIClass {
public:
    explicit UsartSPI(CH32RVHardwareSerial &port) : _port(port) {}

    /* At the last transaction's clock and mode (1 MHz, mode 0 at first). */
    void begin() override;
    /* arduino-esp32's form. -1 for all keeps the port's route; otherwise MISO
     * and MOSI are its RX and TX (setPins()), SCK that route's CK. ss is not
     * used: chip select stays a GPIO. */
    bool begin(int sck, int miso = -1, int mosi = -1, int ss = -1);
    void end() override;

    /* The clock (rounded down to what the USART makes, at most its clock /
     * 16), the bit order and the mode; a new clock or mode is set in place. */
    void beginTransaction(SPISettings settings) override;
    void endTransaction(void) override {}

    uint8_t transfer(uint8_t data) override;
    using ::SPIClass::transfer;

    void setBitOrder(BitOrder order) { _order = order; }
    void setDataMode(uint8_t mode);
    /* The clock the bus runs at (the USART's clock / BRR), 0 while closed. */
    uint32_t clockHz(void) const { return _started ? _port.baudRate() : 0; }

private:
    bool open(void);

    CH32RVHardwareSerial &_port;
    uint32_t _clock = 1000000;
    uint8_t _mode = 0;
};

}  // namespace arduino

using arduino::UsartSPI;
