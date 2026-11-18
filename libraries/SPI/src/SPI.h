/* SPI for CH32 RISC-V.
 *
 * Controller (master) mode, polled. A sketch names the pins with
 * SPI.begin(sck, miso, mosi, ss); SPI.begin() with no arguments is the
 * fallback onto the board's default pins (ch32rv_instances.h). Chip select is
 * not among the bus pins because Arduino drives it as an ordinary GPIO, which
 * is also what lets one bus carry several devices.
 *
 * Peripheral (slave) mode, as ArduinoCore-API defines it (SPIBusMode in
 * HardwareSPI.h; ArduinoCore-zephyr's SPI is the reference): a transaction
 * begun with SPISettings(..., SPI_PERIPHERAL) makes the bus a peripheral, and
 * transfer() then waits for the controller to clock each byte, sending the
 * byte it was given; endTransaction() makes it a controller again. There is
 * no chip select in that API: the bus is selected from beginTransaction() to
 * endTransaction(), MISO driven all along, and the bytes are framed by the
 * transfer() calls alone. Transfers framed by a chip select, queued ahead and
 * moved by interrupts, are CH32RVSPISlave (the hardware SPI) and
 * SoftSPISlave (any pads).
 */
#pragma once

#include "SPIClass.h"
#include "SoftSPI.h"
#include "SoftSPISlave.h"
#include "internal/ch32rv_pins.h"
#include "internal/ch32rv_route.h"
#include "pins_arduino.h"
#include "internal/ch32rv_instances.h"

#include <stdint.h>

/* beginTransaction(SPISettings(clock, order, mode, SPI_PERIPHERAL)) works on
 * every bus here, hardware SPI and SoftSPI alike (above). */
#define SPI_HAS_PERIPHERAL_MODE 1

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
     * the pad setHwCs() drives; without setHwCs() chip select stays a GPIO
     * the sketch (or the library) drives. */
    bool begin(int sck, int miso = -1, int mosi = -1, int ss = -1)
    {
        _ss1 = (uint8_t)(ss < 0 || ss > 0xFE ? 0 : ss + 1);
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

    /* A settings' SPI_PERIPHERAL makes the bus a peripheral until
     * endTransaction() (above); its clock is the controller's, so the
     * settings' frequency is not used then. */
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

    /* Peripheral mode's two halves, called through ch32rv_spi_peripheral
     * (SPIClass.h). Public for the reason ev_irq() is on Wire; not part of
     * the sketch-facing API. */
    void peripheral_mode(bool on);
    uint8_t peripheral_transfer(uint8_t data);

    /* arduino-pico's asynchronous transfer, by DMA: transferAsync() starts
     * it and returns at once, the sketch runs on, and finishedAsync() is true
     * once the last byte is through (and then lets the DMA channels go).
     * send null sends 0xFF, recv null drops what comes in; both buffers have
     * to stay as they are until then, and the bus takes no other transfer
     * meanwhile. false when the bus is closed or a peripheral, a transfer is
     * already running, or another user (a HardwareSerial DMA buffer) holds
     * one of this SPI's DMA channels. Over 65535 bytes it runs in parts, the
     * next started by the finishedAsync() that sees one end. abortAsync()
     * stops it after the byte on the wire. */
    bool transferAsync(const void *send, void *recv, size_t bytes);
    /* arduino-esp32's hardware chip select: the SPI drives its route's NSS
     * pad itself, low from beginTransaction() to endTransaction() (SPE on and
     * off; transfers outside a transaction keep it low from begin()). The pad
     * is begin()'s ss when given - it has to be the route's NSS - or else the
     * route's own. Ignored where the route has no NSS pad (X033 / X035) or
     * ss is another pad; hwCs() says whether it took. */
    void setHwCs(bool use);
    bool hwCs() const { return _hwcs; }
    bool finishedAsync(void);
    void abortAsync(void);
protected:
    void transfer_block(const uint8_t *tx, uint8_t *rx, size_t count) override;
private:
    void apply(uint32_t clock_hz, BitOrder order, uint8_t mode);
    void pads(bool peripheral);

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
    uint8_t _ss1;              /* begin()'s ss + 1, 0 for none (zero-initialised: the bus stays in .bss) */
    uint8_t _nss_pin;          /* the NSS pad setHwCs() drives */
    bool _hwcs;
    bool nss_pad(uint8_t &pin);
    void nss_connect(bool on);
    uint32_t route_value(void) const;
    uint8_t slot_index(void) const;
public:
    /* For SPIHwCs.cpp; not part of the sketch-facing API. */
    void hwcs_release();
private:
    /* A peripheral (SPI_PERIPHERAL) until endTransaction(): 0 no, 1 MSB
     * first, 2 LSB first; 3 after it, the SPI off and the pads inputs until
     * the next controller use. */
    uint8_t _periph = 0;
};

}  // namespace arduino

/* setHwCs()'s end of a transaction (SPIHwCs.cpp); null until setHwCs(). */
extern void (*ch32rv_spi_hwcs)(arduino::CH32RVSPIClass &bus);

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
