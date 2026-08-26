#include "SerialRTT.h"

#include "Arduino.h"

#include <string.h>

using namespace arduino;

/* One ring buffer, as the host expects to find it. The field order and the
 * widths are the contract: the host reads this struct out of RAM, so nothing
 * here may be reordered or packed differently. */
struct rtt_buffer {
    const char *name;
    char *buffer;
    unsigned int size;
    volatile unsigned int write_off;   /* written by us, read by the host */
    volatile unsigned int read_off;    /* written by the host on an up buffer */
    unsigned int flags;
};

struct rtt_control_block {
    char id[16];
    int max_up;
    int max_down;
    struct rtt_buffer up[1];
    struct rtt_buffer down[1];
};

/* Mode 1: when the buffer is full, write what fits and drop the rest. The
 * alternative the format defines is to block until the host catches up, which
 * would hang a sketch whose debugger is unplugged. */
#define RTT_MODE_TRIM 1u

static char up_storage[CH32_RTT_UP_SIZE];
static char down_storage[CH32_RTT_DOWN_SIZE];

/* The name the host tools look for in the ELF. `used` keeps it through
 * --gc-sections, which would otherwise drop a block nothing in the program
 * reads. */
__attribute__((used)) struct rtt_control_block _SEGGER_RTT;

static unsigned int used_bytes(const struct rtt_buffer *b)
{
    unsigned int w = b->write_off;
    unsigned int r = b->read_off;
    return w >= r ? w - r : b->size - r + w;
}

void CH32SerialRTT::begin(unsigned long baudrate, uint16_t config)
{
    (void)baudrate;
    (void)config;

    memset(&_SEGGER_RTT, 0, sizeof(_SEGGER_RTT));
    _SEGGER_RTT.max_up = 1;
    _SEGGER_RTT.max_down = 1;
    _SEGGER_RTT.up[0].name = "Terminal";
    _SEGGER_RTT.up[0].buffer = up_storage;
    _SEGGER_RTT.up[0].size = sizeof(up_storage);
    _SEGGER_RTT.up[0].flags = RTT_MODE_TRIM;
    _SEGGER_RTT.down[0].name = "Terminal";
    _SEGGER_RTT.down[0].buffer = down_storage;
    _SEGGER_RTT.down[0].size = sizeof(down_storage);
    _SEGGER_RTT.down[0].flags = RTT_MODE_TRIM;

    /* The magic last. A host that is scanning RAM matches on the whole string,
     * so writing it only once the descriptors are in place is what keeps it
     * from finding a half-built block. */
    __asm__ volatile ("" ::: "memory");
    memcpy(_SEGGER_RTT.id, "SEGGER RTT", 11);

    _started = true;
}

void CH32SerialRTT::end()
{
    /* The control block is left standing: a host that is already attached is
     * reading these offsets, and pulling them out from under it would look
     * like a corrupt stream rather than an end of one. */
    _started = false;
}

int CH32SerialRTT::available(void)
{
    if (!_started) {
        return 0;
    }
    return (int)used_bytes(&_SEGGER_RTT.down[0]);
}

int CH32SerialRTT::peek(void)
{
    if (!_started || _SEGGER_RTT.down[0].read_off == _SEGGER_RTT.down[0].write_off) {
        return -1;
    }
    return (uint8_t)down_storage[_SEGGER_RTT.down[0].read_off];
}

int CH32SerialRTT::read(void)
{
    int c = peek();
    if (c < 0) {
        return -1;
    }
    unsigned int next = _SEGGER_RTT.down[0].read_off + 1u;
    _SEGGER_RTT.down[0].read_off = next >= _SEGGER_RTT.down[0].size ? 0u : next;
    return c;
}

int CH32SerialRTT::availableForWrite(void)
{
    if (!_started) {
        return 0;
    }
    /* One byte is spent to tell full from empty. */
    return (int)(_SEGGER_RTT.up[0].size - 1u - used_bytes(&_SEGGER_RTT.up[0]));
}

void CH32SerialRTT::flush(void)
{
    if (!_started) {
        return;
    }
    /* Bounded, unlike a UART's flush: with no host attached the buffer never
     * drains and waiting for it would never end. */
    for (uint32_t spin = CH32_RTT_SPIN; spin != 0u; spin--) {
        if (_SEGGER_RTT.up[0].read_off == _SEGGER_RTT.up[0].write_off) {
            return;
        }
    }
}

size_t CH32SerialRTT::write(uint8_t c)
{
    return write(&c, 1);
}

size_t CH32SerialRTT::write(const uint8_t *buffer, size_t size)
{
    if (!_started) {
        return 0;
    }
    struct rtt_buffer *up = &_SEGGER_RTT.up[0];
    unsigned int w = up->write_off;
    unsigned int room = up->size - 1u - used_bytes(up);
    if (size > room) {
        size = room;              /* nobody is draining fast enough; trim */
    }

    /* Up to the end of the ring, then the wrap. Two copies rather than a loop
     * with a modulo per byte, because tracing output is usually a whole line
     * at a time. */
    size_t first = size;
    if (first > up->size - w) {
        first = up->size - w;
    }
    memcpy(up_storage + w, buffer, first);
    memcpy(up_storage, buffer + first, size - first);

    w += size;
    if (w >= up->size) {
        w -= up->size;
    }
    /* Publish the new offset only once the bytes are in memory: the host may
     * read between any two instructions here. */
    __asm__ volatile ("" ::: "memory");
    up->write_off = w;
    return size;
}

/* Its own translation unit, and one a sketch only reaches by including the
 * header - which is where the cost is, buffers included: the global object's
 * vtable keeps every virtual alive whether or not the sketch calls one. */
arduino::CH32SerialRTT SerialRTT;
