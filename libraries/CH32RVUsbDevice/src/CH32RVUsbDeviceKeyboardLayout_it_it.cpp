/* Keyboard layout it-it: linked only when a sketch names it. The table is
 * EspUsbDevice's keymap/keymap_it_it.h, byte for byte. */
#include "CH32RVUsbDevice.h"
#include "keymap/keymap_it_it.h"

const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_IT_IT = {
    0x0410, KEYCODE_TO_UNICODE_IT_IT, 128};
