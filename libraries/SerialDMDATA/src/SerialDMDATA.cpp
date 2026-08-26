#include "SerialDMDATA.h"

#include "Arduino.h"

using namespace arduino;

/* Debug module data registers, seen from the hart - the same pair SerialSDI
 * uses. The address is the QingKe core's hartinfo.dataaddr and differs per
 * family (0xE00000F4 on V2, 0xE0000340 on most V3, 0xE0000380 on V4 and V103),
 * so the board states it from ch32-device-data's debug_data.csv. */
#ifndef CH32_DM_DATA0_ADDR
#error "CH32_DM_DATA0_ADDR is not defined: this board does not say where the \
debug module's data0 is (ch32-device-data debug_data.csv), so SerialDMDATA \
cannot be built for it."
#endif
static volatile uint32_t *const CH32_DM_DATA0 =
    (volatile uint32_t *)CH32_DM_DATA0_ADDR;
static volatile uint32_t *const CH32_DM_DATA1 =
    (volatile uint32_t *)(CH32_DM_DATA0_ADDR + 4u);

/* The status word is the low byte of data0. */
#define ST_PENDING 0x80u          /* we left a frame; the host has not taken it */
#define ST_TIMEOUT 0x40u          /* we gave up waiting for a host */
#define ST_COUNT   0x3fu          /* byte count, biased by 4 */
#define ST_BIAS    4u

/* A word that says "nothing here", written after taking a frame from the host.
 * It reads as a zero-length frame in the other direction, which is what tells
 * the host the mailbox is ours again. */
#define ST_EMPTY   (ST_PENDING | ST_BIAS)

/* The most a host frame can carry, which is what "room to take one" means. */
#define RX_FRAME 3u

uint8_t CH32SerialDMDATA::buffered(void) const
{
    return (uint8_t)(_tail >= _head ? _tail - _head
                                    : sizeof(_rx) - _head + _tail);
}

/* Take the host's bytes out of data0, and invite the next three.
 *
 * The invitation is the point: the host only writes the word after it has
 * taken something out of it, so a target that never leaves anything there
 * never hears anything either. Leaving the empty frame is what a sketch that
 * only reads still does, once per poll.
 *
 * With no room to put a frame we leave it in the register and say nothing,
 * which stops the host: it cannot write again until we answer. That is the
 * flow control. It only goes wrong if write() overwrites the word first, which
 * is why the buffer exists at all. */
void CH32SerialDMDATA::poll(void)
{
    if (sizeof(_rx) - 1u - buffered() < RX_FRAME) {
        return;                   /* no room for a whole frame */
    }
    uint32_t word = *CH32_DM_DATA0;
    if (word & ST_PENDING) {
        return;                   /* our own frame, still waiting to be taken */
    }
    uint32_t n = word & ST_COUNT;
    if (n > ST_BIAS) {
        n -= ST_BIAS;
        if (n > RX_FRAME) {
            n = RX_FRAME;
        }
        for (uint32_t k = 0; k < n; k++) {
            _rx[_tail] = (uint8_t)(word >> (8u * (k + 1u)));
            _tail = (uint8_t)(_tail + 1u >= sizeof(_rx) ? 0u : _tail + 1u);
        }
    }
    *CH32_DM_DATA0 = ST_EMPTY;
}

bool CH32SerialDMDATA::alive(void)
{
    return (*CH32_DM_DATA0 & (ST_PENDING | ST_TIMEOUT)) != (ST_PENDING | ST_TIMEOUT);
}

void CH32SerialDMDATA::begin(unsigned long baudrate, uint16_t config)
{
    (void)baudrate;
    (void)config;
    /* Claim the mailbox. Whatever an earlier session left in it would
     * otherwise be read as a frame - including a latched timeout. */
    *CH32_DM_DATA0 = 0;
    _head = 0;
    _tail = 0;
    _started = true;
}

void CH32SerialDMDATA::end()
{
    _started = false;
}

int CH32SerialDMDATA::available(void)
{
    if (!_started) {
        return 0;
    }
    poll();
    return buffered();
}

int CH32SerialDMDATA::peek(void)
{
    return available() > 0 ? _rx[_head] : -1;
}

int CH32SerialDMDATA::read(void)
{
    int c = peek();
    if (c >= 0) {
        _head = (uint8_t)(_head + 1u >= sizeof(_rx) ? 0u : _head + 1u);
    }
    return c;
}

void CH32SerialDMDATA::flush(void)
{
    if (!_started) {
        return;
    }
    /* Bounded: with no host attached the frame is never taken, and waiting for
     * that would never end. */
    for (uint32_t spin = CH32_DMDATA_SPIN; spin != 0u; spin--) {
        if ((*CH32_DM_DATA0 & ST_PENDING) == 0u) {
            return;
        }
    }
}

size_t CH32SerialDMDATA::write(uint8_t c)
{
    return write(&c, 1);
}

/* One frame: wait for the host to clear bit 7 - it saying it took the last one
 * - then write the payload. data1 holds bytes four to seven, data0 the first
 * three above the status byte, which is why seven is the limit. */
size_t CH32SerialDMDATA::write(const uint8_t *buffer, size_t size)
{
    if (!_started || !alive()) {
        return 0;
    }
    size_t sent = 0;
    while (sent < size) {
        uint32_t word;
        uint32_t spin = CH32_DMDATA_SPIN;
        while ((word = *CH32_DM_DATA0) & ST_PENDING) {
            if (--spin == 0u) {
                /* Nobody is collecting. Mark it so the next write is free
                 * instead of spinning again; a host that attaches later
                 * clears the word and printing resumes. */
                *CH32_DM_DATA0 = word | ST_TIMEOUT;
                return sent;
            }
        }
        /* The wait is also where incoming bytes turn up. Taking them here is
         * what keeps a printing sketch from writing over them. */
        poll();

        size_t chunk = size - sent;
        if (chunk > 7u) {
            chunk = 7u;
        }
        uint8_t p[7] = {0, 0, 0, 0, 0, 0, 0};
        for (size_t k = 0; k < chunk; k++) {
            p[k] = buffer[sent + k];
        }
        *CH32_DM_DATA1 = (uint32_t)p[3] | ((uint32_t)p[4] << 8) |
                         ((uint32_t)p[5] << 16) | ((uint32_t)p[6] << 24);
        *CH32_DM_DATA0 = (ST_PENDING | (uint32_t)(chunk + ST_BIAS)) |
                         ((uint32_t)p[0] << 8) | ((uint32_t)p[1] << 16) |
                         ((uint32_t)p[2] << 24);
        sent += chunk;
    }
    return sent;
}

/* Its own translation unit, and one a sketch only reaches by including the
 * header - which is where the cost is: the global object's vtable keeps every
 * virtual alive whether or not the sketch calls one. */
arduino::CH32SerialDMDATA SerialDMDATA;
