/* Keyboard layout fi-fi: linked only when a sketch names it. The table is
 * EspUsbDevice's keymap/keymap_fi_fi.h, byte for byte. */
#include "CH32RVUsbDevice.h"
#include "keymap/keymap_fi_fi.h"

const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_FI_FI = {
    0x040B, KEYCODE_TO_UNICODE_FI_FI, 128};
