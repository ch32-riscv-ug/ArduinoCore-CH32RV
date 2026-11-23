/* The quad-SPI controller (QSPI1 of CH32V205, QSPI1 / QSPI2 of CH32H41x):
 * commands for an external serial flash (or any QSPI device) on one, two or
 * four data lines, phase by phase - instruction, address, alternate bytes,
 * dummy clocks, data - as the controller itself sees them. No Arduino or
 * arduino-esp32 counterpart.
 *
 *     CH32RVQspi qspi;
 *     qspi.begin(PB2, PB6, PA2, PA3, PA0, PA1);   // SCK, CS, IO0-IO3 (IO2 / IO3 optional)
 *     qspi.setClock(10000000);
 *
 *     CH32RVQspiCommand id;            // JEDEC ID: 9F, three bytes back on one line
 *     id.instruction = 0x9F;
 *     uint8_t jedec[3];
 *     qspi.read(id, jedec, 3);
 *
 *     CH32RVQspiCommand fast;          // fast read quad I/O: EB, address on 4 lines, 6 dummy, data on 4
 *     fast.instruction = 0xEB;
 *     fast.address = 0x1000; fast.addressLines = 4;
 *     fast.dummyCycles = 6; fast.dataLines = 4;
 *     qspi.read(fast, buf, 256);
 *
 * CH32RVQspi(2) is QSPI2 (CH32H41x; on H416 the only one with pads). The
 * pins pick which of the controller's two flashes: CS on the
 * SCSN pad with IO on SIO0-3, or CS on SCSXN with IO on SIOX0-3 (the
 * variant's CH32RV_QSPIn_PADS). Each pad's function is its own (V205 and
 * H41x choose it per pin). IO2 / IO3 are needed only for 4-line phases.
 *
 * The clock is HCLK / (1 .. 256): setClock() takes the fastest at or under
 * the rate asked. Mode 0 by default, setMode(3) for SCK idle high.
 *
 * memoryMap(read command, flash bytes): the flash then reads as memory at
 * mapped() - CH32H41x's QSPI1 at 0x90000000 (EVT QSPI_MemoryMap); elsewhere
 * the address is not in the documents and memoryMap() is false. Any other
 * call ends the mapping. */
#pragma once

#include <Arduino.h>

#if defined(CH32RV_QSPI_PADS_ANY)
#define CH32RV_HAS_QSPI 1
#else
#define CH32RV_HAS_QSPI 0
#endif

struct CH32RVQspiCommand {
    uint8_t instruction = 0;
    uint8_t instructionLines = 1;   /* 0 (none), 1, 2, 4 */
    uint32_t address = 0;
    uint8_t addressLines = 0;       /* 0 (none), 1, 2, 4 */
    uint8_t addressBytes = 3;       /* 1 .. 4 */
    uint32_t alternate = 0;
    uint8_t alternateLines = 0;     /* 0 (none), 1, 2, 4 */
    uint8_t alternateBytes = 1;     /* 1 .. 4 */
    uint8_t dummyCycles = 0;        /* 0 .. 31 */
    uint8_t dataLines = 1;          /* the data phase of read() / write(): 1, 2, 4 */
};

class CH32RVQspi {
public:
    explicit CH32RVQspi(uint8_t number = 1) : _number(number) {}
    ~CH32RVQspi() { end(); }

    bool begin(int sck, int cs, int io0, int io1, int io2 = -1, int io3 = -1);
    void end();

    /* The fastest SCK at or under hz; the rate it gives. */
    uint32_t setClock(uint32_t hz);
    uint32_t clock() const { return _hz; }
    /* SPI mode 0 or 3 (the controller has these two). */
    bool setMode(uint8_t mode);

    /* A command with no data phase (write enable, an erase with its address). */
    bool command(const CH32RVQspiCommand &cmd);
    /* The command, then n bytes out / in on cmd.dataLines. */
    bool write(const CH32RVQspiCommand &cmd, const void *data, size_t n);
    bool read(const CH32RVQspiCommand &cmd, void *data, size_t n);

    bool memoryMap(const CH32RVQspiCommand &readCommand, uint32_t flashBytes);
    const volatile uint8_t *mapped() const { return _mapped; }

private:
    bool start(const CH32RVQspiCommand &cmd, uint32_t fmode, size_t n);
    bool idle();
    void finish();
    bool pad(int pin, uint8_t role);

    uint8_t _number;
    int8_t _index = -1;
    bool _quad = false;
    uint32_t _hz = 0;
    uint8_t _mode = 0;
    const volatile uint8_t *_mapped = nullptr;
    int _pins[6] = {-1, -1, -1, -1, -1, -1};
};
