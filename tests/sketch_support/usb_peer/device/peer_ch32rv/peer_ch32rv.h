/* Types and templates of peer_ch32rv.ino, apart so that they come before the prototypes the Arduino build puts
 * at the top of the sketch. */
#pragma once
#include <stdint.h>

struct Device {
    uint8_t address;          /* 0: none */
    bool announced;
    uint16_t vid, pid, usb;
    uint8_t deviceClass, speed;
    char manufacturer[48], product[48], serial[48];
};

/* Each descriptor of a configuration: f(descriptor) returns false to stop. */
template <typename F> static void walkConfig(const uint8_t *config, uint16_t configLength, F f)
{
    for (uint16_t at = 0; at + 2 <= configLength && config[at] >= 2 && at + config[at] <= configLength; at += config[at])
        if (!f(&config[at])) return;
}
