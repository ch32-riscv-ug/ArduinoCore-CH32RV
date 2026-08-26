/* Two-way serial through the debug module's data registers, in the framing
 * minichlink speaks.
 *
 * Same two words as SerialSDI - the debug module's data0/data1, mapped into
 * the hart's address space - but a different agreement about what the bytes in
 * them mean, and that agreement has a host-to-target direction. The low byte
 * of data0 is a status word: bit 7 says "the target has left something here",
 * bit 6 that the target gave up waiting, and the low bits carry the length.
 * Frames are up to 7 bytes out and 3 bytes in, one at a time, with the probe
 * clearing the word once it has taken a frame.
 *
 * **This cannot be used together with SerialSDI.** Both write the same two
 * registers, and a host reading one framing sees the other as noise. Pick the
 * one whose host tool you have:
 *
 *   SerialSDI      wlink, WCH-LinkUtility          send only, 20 bytes of RAM
 *   SerialDMDATA   minichlink (ch32fun)            two-way,   36 bytes of RAM
 *   SerialRTT      probe-rs attach                 two-way,  364 bytes of RAM
 *
 * The host side here is minichlink's terminal (`minichlink -T`), which this
 * core does not ship - see the README. The protocol is implemented from its
 * documented framing; no ch32fun code is used.
 *
 * Like SerialSDI, this never halts the core and uses no pin. Unlike it, a
 * write that finds no host gives up once and then stays cheap: the timeout is
 * latched into the status word, and a host that attaches later clears it.
 */
#pragma once

#include "api/HardwareSerial.h"

#include <stdint.h>

/* How long write() waits for the probe to take the previous frame, as a spin
 * count. On timeout the frame is dropped and the channel is marked dead until
 * a host clears it, so an unplugged debugger costs this once rather than on
 * every line. */
#ifndef CH32_DMDATA_SPIN
#define CH32_DMDATA_SPIN 200000u
#endif

/* Room to park what the host has sent. The register holds one three-byte frame
 * and printing overwrites it, so without somewhere to put those bytes an echo
 * loop loses two frames out of three. Anything from 3 up works; the default
 * covers a typed line. */
#ifndef CH32_DMDATA_RX_SIZE
#define CH32_DMDATA_RX_SIZE 16u
#endif

/* The offsets are bytes, and a frame has to fit with a slot left over to tell
 * full from empty. */
static_assert(CH32_DMDATA_RX_SIZE >= 4u && CH32_DMDATA_RX_SIZE <= 255u,
              "CH32_DMDATA_RX_SIZE must be between 4 and 255");

namespace arduino {

class CH32SerialDMDATA : public HardwareSerial {
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

    /* True once begin() has claimed the mailbox. It says nothing about whether
     * a host is listening; alive() does, once something has been sent. */
    operator bool() override { return _started; }

    /* False once a write has timed out with nobody collecting. It goes back to
     * true by itself when a host attaches and clears the word, so it is worth
     * re-reading rather than latching in the sketch. */
    bool alive(void);

private:
    /* What the host has sent and the sketch has not read yet. The register
     * itself holds one three-byte frame, and our own next write overwrites it,
     * so anything not taken out of it by then is gone. */
    uint8_t _rx[CH32_DMDATA_RX_SIZE];
    uint8_t _head = 0;            /* next byte to hand to read() */
    uint8_t _tail = 0;            /* next free slot */
    bool _started = false;

    void poll(void);
    uint8_t buffered(void) const;
};

}  // namespace arduino

extern arduino::CH32SerialDMDATA SerialDMDATA;
