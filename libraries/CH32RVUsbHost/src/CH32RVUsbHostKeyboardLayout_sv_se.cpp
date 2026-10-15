/* Keyboard layout sv_se: linked only when a sketch names it. The table is
 * EspUsbHost's keymap/keymap_sv_se.h, byte for byte. */
#include "CH32RVUsbHost.h"
#include "keymap/keymap_sv_se.h"

const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_SV_SE = {
    0x041D, KEYCODE_TO_UNICODE_SV_SE, 128};
