/* Keyboard layout pt_br: linked only when a sketch names it. The table is
 * EspUsbHost's keymap/keymap_pt_br.h, byte for byte. */
#include "CH32RVUsbHost.h"
#include "keymap/keymap_pt_br.h"

const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_PT_BR = {
    0x0416, KEYCODE_TO_UNICODE_PT_BR, 0x90};
