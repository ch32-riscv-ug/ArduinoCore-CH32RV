/* Keyboard layout da_dk: linked only when a sketch names it. The table is
 * EspUsbHost's keymap/keymap_da_dk.h, byte for byte. */
#include "CH32RVUsbHost.h"
#include "keymap/keymap_da_dk.h"

const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_DA_DK = {
    0x0406, KEYCODE_TO_UNICODE_DA_DK, 128};
