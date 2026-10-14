/* Keyboard layout pt-br: linked only when a sketch names it. The table is
 * EspUsbDevice's keymap/keymap_pt_br.h, byte for byte. */
#include "CH32RVUsbDevice.h"
#include "keymap/keymap_pt_br.h"

const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_PT_BR = {
    0x0416, KEYCODE_TO_UNICODE_PT_BR, 0x90};
