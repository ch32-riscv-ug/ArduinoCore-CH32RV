/* Keyboard layout sv-se: linked only when a sketch names it. The table is
 * EspUsbDevice's keymap/keymap_sv_se.h, byte for byte. */
#include "CH32RVUsbDevice.h"
#include "keymap/keymap_sv_se.h"

const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_SV_SE = {
    0x041D, KEYCODE_TO_UNICODE_SV_SE, 128};
