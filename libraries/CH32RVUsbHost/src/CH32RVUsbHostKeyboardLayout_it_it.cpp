/* Keyboard layout it_it: linked only when a sketch names it. The table is
 * EspUsbHost's keymap/keymap_it_it.h, byte for byte. */
#include "CH32RVUsbHost.h"
#include "keymap/keymap_it_it.h"

const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_IT_IT = {
    0x0410, KEYCODE_TO_UNICODE_IT_IT, 128};
