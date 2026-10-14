/* Keyboard layout pt-pt: linked only when a sketch names it. The table is
 * EspUsbDevice's keymap/keymap_pt_pt.h, byte for byte. */
#include "CH32RVUsbDevice.h"
#include "keymap/keymap_pt_pt.h"

const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_PT_PT = {
    0x0816, KEYCODE_TO_UNICODE_PT_PT, 128};
