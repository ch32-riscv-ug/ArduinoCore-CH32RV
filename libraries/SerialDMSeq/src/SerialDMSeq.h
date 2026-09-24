/* Two-way serial through the debug module's data registers, with sequence
 * numbers and a CRC on every word - the "dmseq" framing (oep-spec
 * docs/target-console-dmseq.ja.md, target.console framing 2).
 *
 * The same two words SerialSDI and SerialDMDATA use - the debug module's
 * data0/data1, mapped into the hart's address space - and the same strictly
 * alternating ownership as SerialDMDATA: the target writes data0 only while its
 * bit 7 is clear, the host only while it is set. What dmseq adds is the part
 * minichlink's framing does not have: a 1-bit sequence number each way, a SYN
 * bit for a restarted target, and a CRC-8 right after the payload. With those,
 * a DMI access that goes astray - an answer the probe wrote that never landed,
 * a word read or written corrupted - neither duplicates nor drops a byte. On
 * framing 1 both happened: a CH32L103 behind flying wires got bytes twice, and
 * rewriting to cover for that dropped a repeated character on a CH32V003.
 *
 * **This cannot be used together with SerialSDI or SerialDMDATA.** All three
 * write the same two registers. Pick the one whose host you have:
 *
 *   SerialSDI      wlink, WCH-LinkUtility          send only
 *   SerialDMDATA   minichlink (ch32fun)            two-way
 *   SerialDMSeq    ch32rv --source dmseq, OEP      two-way, checked
 *
 * Up to six bytes out and two in per frame; frames of up to two bytes use data0
 * alone. It never halts the core and uses no pin. A write that finds no host
 * gives up once and then stays cheap until a host answers.
 */
#pragma once

#include "api/HardwareSerial.h"

#include <stdint.h>

/* How long write() waits for the host to answer the previous frame, in
 * milliseconds: short until a host has answered once, long after, so a host
 * that polls late - over USB/IP, next to other probes - is waited for rather
 * than having output dropped. On a timeout the frame stays posted with TO set
 * and later writes are dropped until a host answers it. A synced host that
 * polls at least once a second loses nothing. */
#ifndef CH32_DMSEQ_WAIT_MS
#define CH32_DMSEQ_WAIT_MS 20u
#endif
#ifndef CH32_DMSEQ_HOST_WAIT_MS
#define CH32_DMSEQ_HOST_WAIT_MS 1000u
#endif

/* Room to park what the host has sent. A frame carries two bytes; the host is
 * held off (its frame is not taken, so it sends it again) while there is no
 * room for one. */
#ifndef CH32_DMSEQ_RX_SIZE
#define CH32_DMSEQ_RX_SIZE 16u
#endif

static_assert(CH32_DMSEQ_RX_SIZE >= 4u && CH32_DMSEQ_RX_SIZE <= 255u,
              "CH32_DMSEQ_RX_SIZE must be between 4 and 255");

namespace arduino {

class CH32SerialDMSeq : public HardwareSerial {
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
     * a host is listening; alive() does. */
    operator bool() override { return _started; }

    /* False while writes are being dropped because nobody answered in time. It
     * goes back to true by itself when a host answers, so re-read it rather
     * than latching it in the sketch. */
    bool alive(void) { return !_latched; }

private:
    uint8_t _rx[CH32_DMSEQ_RX_SIZE];
    uint8_t _head = 0;            /* next byte to hand to read() */
    uint8_t _tail = 0;            /* next free slot */
    uint8_t _s = 0;               /* sequence bit of the frame posted (or next) */
    uint8_t _last_h = 1;          /* sequence bit of the last host payload taken */
    bool _started = false;
    bool _posted = false;         /* a frame is in data0, waiting for an answer */
    bool _syn = true;             /* no valid answer since begin() */
    bool _host = false;           /* a host has answered since begin() or the last timeout */
    bool _latched = false;        /* timed out; writes are dropped until an answer */
    bool _long = false;           /* the posted frame reaches data1 */
    bool _wrote = false;          /* the sketch wrote since available() last looked */
    uint32_t _w0 = 0, _w1 = 0;    /* the posted frame, to post again */
    uint32_t _stale_ms = 0;       /* while latched: when the frame last went up */
    uint32_t _stale_calls = 0;    /* ... and calls since, for when millis() stands still */
    uint32_t _stale_limit = 1;

    uint8_t buffered(void) const;
    uint32_t waitPolls(void) const;
    void post(const uint8_t *p, uint8_t n);
    void repost(void);
    bool service(void);
    void stale(void);
    bool waitAnswered(void);
};

}  // namespace arduino

extern arduino::CH32SerialDMSeq SerialDMSeq;
