/* Keyboard layout nl-nl: linked only when a sketch names it. The table is
 * EspUsbDevice's keymap/keymap_nl_nl.h, byte for byte. */
#include "CH32RVUsbDevice.h"
#include "keymap/keymap_nl_nl.h"

const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_NL_NL = {
    0x0413, KEYCODE_TO_UNICODE_NL_NL, 128};
