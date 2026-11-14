/* Types and templates of peer_ch32rv.ino, apart so that they come before the prototypes the Arduino build puts
 * at the top of the sketch. */
#pragma once
#include <stdint.h>
#include <CH32RVUsbHost.h>

struct Device {
    uint8_t address;          /* 0: none */
    bool announced;
    uint16_t vid, pid, usb;
    uint8_t deviceClass, speed;
    char manufacturer[CH32RV_USB_HOST_STRING_MAX];
    char product[CH32RV_USB_HOST_STRING_MAX];
    char serial[CH32RV_USB_HOST_STRING_MAX];
};
