/* SPI for CH32 RISC-V.
 *
 * Controller (master) mode, polled. A sketch names the pins with
 * SPI.begin(sck, miso, mosi, ss); SPI.begin() with no arguments is the
 * fallback onto the board's default pins (ch32rv_instances.h). Chip select is
 * not among the bus pins because Arduino drives it as an ordinary GPIO, which
 * is also what lets one bus carry several devices.
 *
 * Peripheral (slave) mode is a library of its own, CH32RVSPISlave, so
 * SPI_HAS_PERIPHERAL_MODE is deliberately not defined here.
 */
#pragma once

#include "SPIClass.h"
#include "SoftSPI.h"
#include "internal/ch32rv_pins.h"
#include "internal/ch32rv_route.h"
#include "pins_arduino.h"
#include "internal/ch32rv_instances.h"

#include <stdint.h>

/* Legacy divider names. AVR's SPI.setClockDivider() took these, and enough
 * libraries still call it that leaving them out breaks compilation for no
 * gain. The divider is against F_CPU, as it is there. */

namespace arduino {

/* One object is one bus. Which SPI it drives is decided in begin(), as for
 * HardwareSerial and Wire (ADR draft 0015): the object holds state only, each
 * SPI's constants sit in a const table in SPI.cpp, and an SPI set to Disable
 * for the board (CH32RV_SPIn_MODE = 0) is not in that table.
 *
 * `number` is the name's position (SPI = 0, SPI1 = 1, SPI2 = 2), which is also
 * its row in the table of enabled SPIs: begin(sck, miso, mosi) takes any free
 * SPI those pins are a route of, this row first; begin() without pins takes
 * this row on its default pins (ch32rv_instances.h). */
class CH32RVSPIClass : public ::SPIClass {
public:
    explicit CH32RVSPIClass(uint8_t number) : _number(number) {}

    /* A hardware bus is the predefined object (SPI, SPI1 ...), never one on
     * the heap: no operator new, and a delete with nothing to give back.
     * HardwareSPI's virtual destructor otherwise puts a deleting destructor
     * in the vtable that calls free() - newlib's allocator and its reent,
     * 76 B of RAM and 200 B of flash on V003, in every SPI sketch. */
    static void *operator new(size_t) = delete;
    static void operator delete(void *) noexcept {}

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
    void end() override;

    uint8_t transfer(uint8_t data) override;
    using ::SPIClass::transfer;

    void beginTransaction(SPISettings settings) override;
    void endTransaction(void) override;

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
    uint8_t _mode = 0;
};

}  // namespace arduino

/* As many names as the board has enabled SPIs (ch32rv_instances.h), in the
 * Arduino ecosystem's order: the bare name first. An SPI set to Software in
 * the board menu makes its name a SoftSPI. */
#if CH32RV_SPI_COUNT > 0
#define SPI_INTERFACES_COUNT CH32RV_SPI_COUNT
#if CH32RV_SPI_SOFT_N(CH32RV_SPI_ROW0_N)
extern arduino::SoftSPI SPI;               /* SPIInstance.cpp */
#else
extern arduino::CH32RVSPIClass SPI;        /* SPIInstance.cpp */
#endif
#endif
#if CH32RV_SPI_COUNT > 1
#if CH32RV_SPI_SOFT_N(CH32RV_SPI_ROW1_N)
extern arduino::SoftSPI SPI1;              /* SPI1Instance.cpp */
#else
extern arduino::CH32RVSPIClass SPI1;       /* SPI1Instance.cpp */
#endif
#endif
#if CH32RV_SPI_COUNT > 2
#if CH32RV_SPI_SOFT_N(CH32RV_SPI_ROW2_N)
extern arduino::SoftSPI SPI2;              /* SPI2Instance.cpp */
#else
extern arduino::CH32RVSPIClass SPI2;       /* SPI2Instance.cpp */
#endif
#endif
