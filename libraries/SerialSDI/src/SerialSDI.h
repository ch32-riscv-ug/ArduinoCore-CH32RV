/* Serial output through the debug module's data registers (WCH's "SDI print").
 *
 * The two words at 0xE0000380/0x384 are the RISC-V debug module's abstract
 * data registers, mapped into the hart's address space. hartinfo says so on
 * these parts (dataaccess=1, datasize=2, dataaddr=0x380), so the addresses are
 * a fact the hardware reports rather than a magic number. A WCH-LinkE told to
 * do so polls them over the debug transport and forwards what it finds to its
 * own USB CDC port.
 *
 * What that buys: **no UART, no pin, no wiring**, and the core is never
 * halted. What it costs: the host has to have enabled it, which today means a
 * tool other than probe-rs (see docs/todo.ja.md).
 *
 * This is a separate Stream rather than a compile-time switch inside
 * HardwareSerial - which is how WCH's own examples and the older community
 * core do it - so a sketch can use both at once: the UART for the user and
 * SerialSDI for tracing.
 *
 * Receive is not implemented. The protocol has a host-to-target direction, but
 * nothing has been verified for it here, and a read() that silently returns
 * nothing is easier to reason about than one that half-works.
 */
#pragma once

#include "api/HardwareSerial.h"

#include <stdint.h>

/* How long write() waits for the probe to take the previous frame, as a spin
 * count. The vendor's implementation waits forever, which turns "I unplugged
 * the debugger" into "my sketch hangs"; this one gives up and drops the bytes,
 * the way a UART with nobody listening does. */
#ifndef CH32_SDI_SPIN
#define CH32_SDI_SPIN 200000u
#endif

namespace arduino {

class CH32SerialSDI : public HardwareSerial {
public:
    /* The baud rate is meaningless here - there is no wire - and is accepted
     * only so that a sketch can swap this in for Serial without edits. */
    void begin(unsigned long baudrate) override { begin(baudrate, 0); }
    void begin(unsigned long baudrate, uint16_t config) override;
    void end() override;

    int available(void) override { return 0; }
    int peek(void) override { return -1; }
    int read(void) override { return -1; }
    void flush(void) override {}
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    using Print::write;

    /* True once begin() has claimed the mailbox. It says nothing about whether
     * a host is listening: the target cannot tell. */
    operator bool() override { return _started; }

private:
    bool _started = false;
};

}  // namespace arduino

extern arduino::CH32SerialSDI SerialSDI;
