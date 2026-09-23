#include "SerialDMSeq.h"

#include "Arduino.h"

using namespace arduino;

/* Debug module data registers, seen from the hart - the pair SerialSDI and
 * SerialDMDATA use. The board states the address (ch32-device-data
 * debug_data.csv). */
#ifndef CH32_DM_DATA0_ADDR
#error "CH32_DM_DATA0_ADDR is not defined: this board does not say where the \
debug module's data0 is (ch32-device-data debug_data.csv), so SerialDMSeq \
cannot be built for it."
#endif
static volatile uint32_t *const CH32_DM_DATA0 =
    (volatile uint32_t *)CH32_DM_DATA0_ADDR;
static volatile uint32_t *const CH32_DM_DATA1 =
    (volatile uint32_t *)(CH32_DM_DATA0_ADDR + 4u);

/* data0's low byte. Target frame: T TO S A SYN N(3). Host answer: 0 0 K H 0 M(3). */
#define ST_TARGET  0x80u          /* the word is the target's frame */
#define ST_TIMEOUT 0x40u          /* the target stopped waiting for this frame */
#define ST_SEQ     0x20u          /* S (frame) / K (answer) */
#define ST_ACK     0x10u          /* A (frame) / H (answer) */
#define ST_SYN     0x08u
#define ST_LEN     0x07u

#define FRAME_MAX  6u             /* payload bytes a frame carries */
#define ANSWER_MAX 2u             /* payload bytes an answer carries */

/* CRC-8, poly 0x07, init 0xFF, over the status byte and the payload; stored
 * right after the payload. Four bits at a time from a 16-entry table: as fast
 * as a 256-byte table for 24 bytes more than the bit loop (measured,
 * oep-spec experiments/dm-console-seq). init 0xFF makes an all-zero word
 * invalid, so a data0 that holds nothing (a V4 with no debugger attached reads
 * 0) is never taken for an answer. */
static uint8_t crc8(const uint8_t *p, uint8_t n)
{
    static const uint8_t t[16] = {0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15,
                                  0x38, 0x3f, 0x36, 0x31, 0x24, 0x23, 0x2a, 0x2d};
    uint8_t crc = 0xff;
    while (n--) {
        crc ^= *p++;
        crc = (uint8_t)(crc << 4) ^ t[crc >> 4];
        crc = (uint8_t)(crc << 4) ^ t[crc >> 4];
    }
    return crc;
}

uint8_t CH32SerialDMSeq::buffered(void) const
{
    return (uint8_t)(_tail >= _head ? _tail - _head
                                    : sizeof(_rx) - _head + _tail);
}

/* The waits are counted in polls of the register, not read off a clock: they
 * must still end with interrupts masked, where millis() stands still. One poll
 * measured about 10 cycles on CH32V003; 8 is assumed, so a wait lasts at least
 * as long as asked. */
uint32_t CH32SerialDMSeq::waitPolls(void) const
{
    const uint64_t per_ms = (uint64_t)F_CPU / 1000u / 8u;
    const uint64_t polls =
        per_ms * (_host ? CH32_DMSEQ_HOST_WAIT_MS : CH32_DMSEQ_WAIT_MS);
    return polls > 0xffffffffu ? 0xffffffffu : (polls ? (uint32_t)polls : 1u);
}

/* Build a frame and hand it over: data1 first (when the frame reaches it), then
 * data0, whose bit 7 gives it to the host. */
void CH32SerialDMSeq::post(const uint8_t *p, uint8_t n)
{
    uint8_t b[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    b[0] = (uint8_t)(ST_TARGET | (_s ? ST_SEQ : 0u) | (_last_h ? ST_ACK : 0u) |
                     (_syn ? ST_SYN : 0u) | n);
    for (uint8_t i = 0; i < n; i++) {
        b[1 + i] = p[i];
    }
    b[1 + n] = crc8(b, (uint8_t)(1 + n));
    _long = n >= 3;
    _w0 = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
          ((uint32_t)b[3] << 24);
    _w1 = (uint32_t)b[4] | ((uint32_t)b[5] << 8) | ((uint32_t)b[6] << 16) |
          ((uint32_t)b[7] << 24);
    repost();
    _posted = true;
}

void CH32SerialDMSeq::repost(void)
{
    if (_long) {
        *CH32_DM_DATA1 = _w1;
    }
    *CH32_DM_DATA0 = _w0;
}

/* Look at data0 once. Returns true when no frame is outstanding any more.
 *
 * A valid answer acknowledges the frame and may carry host bytes. An invalid
 * one - a corrupted word, an answer to the other sequence bit, whatever an
 * attach or a flash left behind - gets the same frame posted again, which the
 * host recognises as a duplicate. */
bool CH32SerialDMSeq::service(void)
{
    if (!_posted) {
        return true;
    }
    const uint32_t w = *CH32_DM_DATA0;
    if (w & ST_TARGET) {
        return false;             /* still ours: not answered yet */
    }
    const uint8_t a[4] = {(uint8_t)w, (uint8_t)(w >> 8), (uint8_t)(w >> 16),
                          (uint8_t)(w >> 24)};
    const uint8_t m = a[0] & ST_LEN;
    /* M before the CRC: there is no byte 1+M past the word. */
    const bool valid = m <= ANSWER_MAX && crc8(a, (uint8_t)(1 + m)) == a[1 + m] &&
                       ((a[0] & ST_SEQ) != 0) == (_s != 0);
    if (!valid) {
        repost();
        return false;
    }
    _posted = false;
    _syn = false;
    _host = true;
    _latched = false;
    _s ^= 1u;
    const uint8_t h = (a[0] & ST_ACK) ? 1u : 0u;
    if (m && h != _last_h && sizeof(_rx) - 1u - buffered() >= m) {
        for (uint8_t i = 0; i < m; i++) {
            _rx[_tail] = a[1 + i];
            _tail = (uint8_t)(_tail + 1u >= sizeof(_rx) ? 0u : _tail + 1u);
        }
        _last_h = h;
    }                             /* no room: not taken, the host sends it again */
    return true;
}

/* Wait for the posted frame to be answered, within the bound. On a timeout the
 * frame is posted again with TO set - same sequence bit and payload, CRC redone
 * - and left there: a host that attaches later answers it and printing resumes. */
bool CH32SerialDMSeq::waitAnswered(void)
{
    if (_latched) {
        return service();         /* free until a host answers */
    }
    for (uint32_t spin = waitPolls(); spin != 0u; spin--) {
        if (service()) {
            return true;
        }
    }
    _latched = true;
    _host = false;
    uint8_t b[8] = {(uint8_t)(_w0 | ST_TIMEOUT), (uint8_t)(_w0 >> 8), (uint8_t)(_w0 >> 16),
                    (uint8_t)(_w0 >> 24), (uint8_t)_w1, (uint8_t)(_w1 >> 8),
                    (uint8_t)(_w1 >> 16), (uint8_t)(_w1 >> 24)};
    const uint8_t n = b[0] & ST_LEN;
    b[1 + n] = crc8(b, (uint8_t)(1 + n));
    _w0 = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
          ((uint32_t)b[3] << 24);
    _w1 = (uint32_t)b[4] | ((uint32_t)b[5] << 8) | ((uint32_t)b[6] << 16) |
          ((uint32_t)b[7] << 24);
    repost();
    return false;
}

void CH32SerialDMSeq::begin(unsigned long baudrate, uint16_t config)
{
    (void)baudrate;
    (void)config;
    /* Claim the mailbox. A zero word is never a valid answer (the CRC starts at
     * 0xFF), so nothing an earlier session left can be taken for one. */
    *CH32_DM_DATA0 = 0;
    _head = _tail = 0;
    _s = 0;
    _last_h = 1;
    _posted = false;
    _syn = true;
    _host = false;
    _latched = false;
    _long = false;
    _wrote = false;
    _started = true;
}

void CH32SerialDMSeq::end()
{
    _started = false;
}

/* Take what an answer brought, and leave an empty frame - the host's turn to
 * answer, which is how input arrives - but only when idle. A sketch that
 * alternates available() and print() would otherwise pay a round trip for an
 * empty frame before every print; while it prints, input rides on the answers
 * to its data frames. */
int CH32SerialDMSeq::available(void)
{
    if (!_started) {
        return 0;
    }
    service();
    if (!_posted && !_latched) {
        if (!_wrote) {
            post(nullptr, 0);
        }
        _wrote = false;
    }
    return buffered();
}

int CH32SerialDMSeq::peek(void)
{
    if (!_started) {
        return -1;
    }
    service();
    return buffered() ? _rx[_head] : -1;
}

int CH32SerialDMSeq::read(void)
{
    const int c = peek();
    if (c >= 0) {
        _head = (uint8_t)(_head + 1u >= sizeof(_rx) ? 0u : _head + 1u);
    }
    return c;
}

void CH32SerialDMSeq::flush(void)
{
    if (_started) {
        waitAnswered();
    }
}

size_t CH32SerialDMSeq::write(uint8_t c)
{
    return write(&c, 1);
}

size_t CH32SerialDMSeq::write(const uint8_t *buffer, size_t size)
{
    if (!_started) {
        return 0;
    }
    size_t sent = 0;
    while (sent < size) {
        if (!waitAnswered()) {
            return sent;
        }
        const size_t chunk = size - sent > FRAME_MAX ? FRAME_MAX : size - sent;
        post(buffer + sent, (uint8_t)chunk);
        _wrote = true;
        sent += chunk;
    }
    return sent;
}

/* Its own translation unit, and one a sketch only reaches by including the
 * header - the global object's vtable keeps every virtual alive whether or not
 * the sketch calls one. */
arduino::CH32SerialDMSeq SerialDMSeq;
