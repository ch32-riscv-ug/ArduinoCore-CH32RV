/* SPI for CH32 RISC-V.
 *
 * Controller (master) mode, polled. The default pins come from the variant's
 * CH32RV_SPIn_SCK/MISO/MOSI; chip select is not among them because Arduino
 * drives it as an ordinary GPIO, which is also what lets one bus carry several
 * devices.
 *
 * Peripheral (slave) mode is not implemented, so SPI_HAS_PERIPHERAL_MODE is
 * deliberately not defined - a library can test for it rather than discover it
 * at runtime.
 */
#pragma once

#include "api/HardwareSPI.h"
#include "ch32rv_pins.h"
#include "ch32rv_route.h"
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

/* One object is one bus. Which SPI it drives is decided in begin(), as for
 * HardwareSerial and Wire (ADR draft 0015): the object holds state only, each
 * SPI's constants sit in a const table in SPI.cpp, and an SPI set to Disable
 * for the board (CH32RV_SPIn_MODE = 0) is not in that table.
 *
 * Provisional rule until names and instances are tied together for good: a
 * begin() without pins binds SPI / SPI1 / SPI2 to the bus they have always
 * named, on its default route - or the fixed one when the board sets FIX.
 * begin(sck, miso, mosi) binds to whichever enabled SPI has those pins. */
class CH32RVSPIClass : public HardwareSPI {
public:
    explicit CH32RVSPIClass(uint8_t number) : _number(number) {}

    void begin() override;
    /* arduino-esp32's form: the pins (SCK, MISO, MOSI) named in the call.
     * All -1 keeps the pins the bus has; the three have to be one route, and
     * false leaves the bus closed. ss is accepted for the same reason and not
     * used: chip select stays a GPIO the sketch (or the library) drives. */
    bool begin(int sck, int miso = -1, int mosi = -1, int ss = -1);
    void end() override;

    uint8_t transfer(uint8_t data) override;
    uint16_t transfer16(uint16_t data) override;
    void transfer(void *buf, size_t count) override;

    /* arduino-esp32's buffer and wide-word transfers. A word's bytes go in
     * the bit order's direction, as transfer16() does (MSBFIRST: high first). */
    uint32_t transfer32(uint32_t data);
    void transferBytes(const uint8_t *data, uint8_t *out, uint32_t size);
    void writeBytes(const uint8_t *data, uint32_t size);
    void write(uint8_t data) { (void)transfer(data); }
    void write16(uint16_t data) { (void)transfer16(data); }
    void write32(uint32_t data) { (void)transfer32(data); }

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

    /* Move this bus onto another of its pin routes. false, and nothing
     * changed, when the route does not exist on this series; setPins()
     * additionally refuses pins that do not all belong to one route, which is
     * the only thing the hardware can select. Calling either after begin()
     * reopens the bus and hands the old pads back as inputs. */
    bool setRoute(uint8_t route);
    bool setPins(int sck, int miso, int mosi);

    /* The SPI number this bus is bound to, 0 while closed. */
    uint8_t peripheral(void) const;

    /* Pre-transaction API. Still used by a lot of library code. */
    void setBitOrder(BitOrder order);
    void setDataMode(uint8_t mode);
    void setClockDivider(uint32_t divider);

private:
    void apply(uint32_t clock_hz, BitOrder order, uint8_t mode);

    const uint8_t _number;
    /* The fields below start at zero because a bus is a global object, in
     * .bss: no initialiser, so the constructor does not store the zeros again.
     * 0 = closed; otherwise 1 + index into the SPI table. */
    uint8_t _slot;
    /* Route chosen by setPins() / setRoute() for the next begin(), already
     * looked up, so begin() never searches. nullptr = the default. */
    uint8_t _want_index;
    const ch32rv_route_t *_want;
    /* Of the bound SPI; 0 while closed. */
    uint32_t _base;
    uint8_t _sck_pin;
    uint8_t _miso_pin;
    uint8_t _mosi_pin;

    uint32_t _clock_hz = 4000000;
    BitOrder _order = MSBFIRST;
    uint8_t _mode = 0;
    bool _started = false;
};

}  // namespace arduino

#if defined(CH32RV_SPI1_SCK) + defined(CH32RV_SPI2_SCK) + defined(CH32RV_SPI3_SCK) == 3
#define SPI_INTERFACES_COUNT 3
#elif defined(CH32RV_SPI1_SCK) + defined(CH32RV_SPI2_SCK) + defined(CH32RV_SPI3_SCK) == 2
#define SPI_INTERFACES_COUNT 2
#elif defined(CH32RV_SPI1_SCK) || defined(CH32RV_SPI2_SCK) || defined(CH32RV_SPI3_SCK)
#define SPI_INTERFACES_COUNT 1
#endif

/* Bus order, as everywhere else in the Arduino ecosystem: the bare name is the
 * first bus. */
#if defined(CH32RV_SPI1_SCK)
extern arduino::CH32RVSPIClass SPI;        /* SPI1 */
#if defined(CH32RV_SPI2_SCK)
extern arduino::CH32RVSPIClass SPI1;       /* SPI2 */
#endif
#if defined(CH32RV_SPI3_SCK)
extern arduino::CH32RVSPIClass SPI2;       /* SPI3 */
#endif
#elif defined(CH32RV_SPI2_SCK)
extern arduino::CH32RVSPIClass SPI;        /* SPI2, on a part with no SPI1 */
#if defined(CH32RV_SPI3_SCK)
extern arduino::CH32RVSPIClass SPI1;       /* SPI3 */
#endif
#endif
