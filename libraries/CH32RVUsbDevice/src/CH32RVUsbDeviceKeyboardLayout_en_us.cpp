/* Keyboard layout en-us: linked only when a sketch names it. The table is
 * EspUsbDevice's keymap/keymap_en_us.h, byte for byte. */
#include "CH32RVUsbDevice.h"
#include "keymap/keymap_en_us.h"

const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_EN_US = {
    0x0409, KEYCODE_TO_UNICODE_EN_US, 128};
