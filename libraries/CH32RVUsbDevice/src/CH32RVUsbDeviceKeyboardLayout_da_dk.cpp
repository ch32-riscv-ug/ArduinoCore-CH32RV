/* Keyboard layout da-dk: linked only when a sketch names it. The table is
 * EspUsbDevice's keymap/keymap_da_dk.h, byte for byte. */
#include "CH32RVUsbDevice.h"
#include "keymap/keymap_da_dk.h"

const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_DA_DK = {
    0x0406, KEYCODE_TO_UNICODE_DA_DK, 128};
