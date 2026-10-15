/* Keyboard layout nb_no: linked only when a sketch names it. The table is
 * EspUsbHost's keymap/keymap_nb_no.h, byte for byte. */
#include "CH32RVUsbHost.h"
#include "keymap/keymap_nb_no.h"

const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_NB_NO = {
    0x0414, KEYCODE_TO_UNICODE_NB_NO, 128};
