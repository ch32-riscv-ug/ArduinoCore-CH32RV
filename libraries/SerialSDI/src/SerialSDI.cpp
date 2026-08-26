#include "SerialSDI.h"

#include "Arduino.h"

using namespace arduino;

/* Debug module data registers, seen from the hart. The address is the QingKe
 * core's hartinfo.dataaddr and it is NOT the same everywhere: the V2 families
 * map DATA0/DATA1 at 0xE00000F4/0xF8, most V3 families at 0xE0000340/0x344,
 * and the V4 families (with V103) at 0xE0000380/0x384. The board says which,
 * from ch32-device-data's debug_data.csv - a default here would be silently
 * wrong on two families out of three, writing into some other part of the
 * debug module. */
#ifndef CH32_DM_DATA0_ADDR
#error "CH32_DM_DATA0_ADDR is not defined: this board does not say where the \
debug module's data0 is (ch32-device-data debug_data.csv), so SerialSDI cannot \
be built for it."
#endif
static volatile uint32_t *const CH32_DM_DATA0 =
    (volatile uint32_t *)CH32_DM_DATA0_ADDR;
static volatile uint32_t *const CH32_DM_DATA1 =
    (volatile uint32_t *)(CH32_DM_DATA0_ADDR + 4u);

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

/* Its own translation unit, and one a sketch only reaches by including the
 * header - which is where the cost is: the global object's vtable keeps every
 * virtual alive whether or not the sketch calls one. */
arduino::CH32SerialSDI SerialSDI;
