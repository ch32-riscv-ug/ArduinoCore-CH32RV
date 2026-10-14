/* Keyboard layout en-gb: linked only when a sketch names it. The table is
 * EspUsbDevice's keymap/keymap_en_gb.h, byte for byte. */
#include "CH32RVUsbDevice.h"
#include "keymap/keymap_en_gb.h"

const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_EN_GB = {
    0x0809, KEYCODE_TO_UNICODE_EN_GB, 128};
