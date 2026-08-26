/* Serial over a RAM ring buffer that the debug probe reads while the core runs
 * ("RTT").
 *
 * The target keeps a small control block in RAM: a magic string, then a
 * descriptor per buffer holding its address, its size, and the two offsets a
 * ring buffer needs. The host finds the block - by the _SEGGER_RTT symbol in
 * the ELF, or by scanning RAM for the magic - and then just reads and writes
 * memory over the debug transport. Nothing on the target is interrupted: the
 * debug module does the accesses, so **the core is never halted** and no pin
 * is used.
 *
 * What that buys over SerialSDI: the host side is probe-rs, which is what this
 * core already flashes with (`probe-rs attach`), and the channel is genuinely
 * two-way, so read() works. What it costs: RAM - the buffers are real memory,
 * CH32_RTT_UP_SIZE + CH32_RTT_DOWN_SIZE plus about 70 bytes of control block.
 * On a 2 KB part that is worth thinking about; SerialDMDATA does the same job
 * for 36 bytes but needs a different host tool.
 *
 * The layout is the publicly documented one and the symbol carries the name
 * the host tools look for. No SEGGER code is used here.
 */
#pragma once

#include "api/HardwareSerial.h"

#include <stdint.h>

/* Target to host. The default is small enough for the 2 KB parts and still
 * holds several lines; a host that polls a few times a second wants more.
 * Override with -DCH32_RTT_UP_SIZE=... (build_opt.h, or --build-property
 * build.extra_flags). Sizes need not be a power of two. */
#ifndef CH32_RTT_UP_SIZE
#define CH32_RTT_UP_SIZE 256u
#endif

/* Host to target. Keystrokes, so it can be tiny. */
#ifndef CH32_RTT_DOWN_SIZE
#define CH32_RTT_DOWN_SIZE 16u
#endif

/* How long flush() waits for the host to drain the buffer, as a spin count.
 * write() itself never waits - a full buffer with nobody reading drops, the
 * way a UART with nobody listening does - so this is the only place a missing
 * host can cost time. */
#ifndef CH32_RTT_SPIN
#define CH32_RTT_SPIN 200000u
#endif

/* A ring spends one slot telling full from empty, so two is the floor at which
 * a byte can still be carried. */
static_assert(CH32_RTT_UP_SIZE >= 2u && CH32_RTT_DOWN_SIZE >= 2u,
              "CH32_RTT_UP_SIZE and CH32_RTT_DOWN_SIZE must be at least 2");

namespace arduino {

class CH32SerialRTT : public HardwareSerial {
public:
    /* The baud rate is meaningless here - there is no wire - and is accepted
     * only so that a sketch can swap this in for Serial without edits. */
    void begin(unsigned long baudrate) override { begin(baudrate, 0); }
    void begin(unsigned long baudrate, uint16_t config) override;
    void end() override;

    int available(void) override;
    int peek(void) override;
    int read(void) override;
    void flush(void) override;
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    using Print::write;

    /* How much more write() would take right now. Unlike a UART's, this number
     * only moves when a host is actually reading. */
    int availableForWrite(void) override;

    /* True once begin() has published the control block. It says nothing about
     * whether a host is attached: the target cannot tell. */
    operator bool() override { return _started; }

private:
    bool _started = false;
};

}  // namespace arduino

extern arduino::CH32SerialRTT SerialRTT;
