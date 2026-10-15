/* Keyboard layout pt_pt: linked only when a sketch names it. The table is
 * EspUsbHost's keymap/keymap_pt_pt.h, byte for byte. */
#include "CH32RVUsbHost.h"
#include "keymap/keymap_pt_pt.h"

const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_PT_PT = {
    0x0816, KEYCODE_TO_UNICODE_PT_PT, 128};
