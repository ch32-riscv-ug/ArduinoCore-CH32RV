#include "SerialSDI.h"

#include "Arduino.h"

using namespace arduino;

/* Debug module data registers, seen from the hart. */
static volatile uint32_t *const CH32_DM_DATA0 = (volatile uint32_t *)0xE0000380u;
static volatile uint32_t *const CH32_DM_DATA1 = (volatile uint32_t *)0xE0000384u;

/* One frame: wait for DATA0 to read back zero - the probe saying it took the
 * last one - then write the payload high and the length low. Seven bytes is
 * what the two words hold once the length has taken a byte. */
static bool sdi_frame(const uint8_t *b, size_t n)
{
    uint32_t spin = CH32_SDI_SPIN;
    while (*CH32_DM_DATA0 != 0u) {
        if (--spin == 0u) {
            return false;
        }
    }
    uint8_t p[7] = {0, 0, 0, 0, 0, 0, 0};
    for (size_t k = 0; k < n; k++) {
        p[k] = b[k];
    }
    *CH32_DM_DATA1 = (uint32_t)p[3] | ((uint32_t)p[4] << 8) |
                     ((uint32_t)p[5] << 16) | ((uint32_t)p[6] << 24);
    *CH32_DM_DATA0 = (uint32_t)n | ((uint32_t)p[0] << 8) |
                     ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 24);
    return true;
}

void CH32SerialSDI::begin(unsigned long baudrate, uint16_t config)
{
    (void)baudrate;
    (void)config;
    /* Claim the mailbox. Whatever an earlier session left in it would
     * otherwise be read as a frame. */
    *CH32_DM_DATA0 = 0;
    _started = true;
}

void CH32SerialSDI::end()
{
    _started = false;
}

size_t CH32SerialSDI::write(uint8_t c)
{
    return write(&c, 1);
}

size_t CH32SerialSDI::write(const uint8_t *buffer, size_t size)
{
    if (!_started) {
        return 0;
    }
    size_t sent = 0;
    while (sent < size) {
        size_t chunk = size - sent;
        if (chunk > 7u) {
            chunk = 7u;
        }
        if (!sdi_frame(buffer + sent, chunk)) {
            return sent;          /* nobody is collecting; drop the rest */
        }
        sent += chunk;
    }
    return sent;
}

/* Its own translation unit, so a sketch that never mentions SerialSDI does not
 * link it. */
arduino::CH32SerialSDI SerialSDI;
