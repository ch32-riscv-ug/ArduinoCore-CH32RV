/* Keyboard layout fr_fr: linked only when a sketch names it. The table is
 * EspUsbHost's keymap/keymap_fr_fr.h, byte for byte. */
#include "CH32RVUsbHost.h"
#include "keymap/keymap_fr_fr.h"

const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_FR_FR = {
    0x040C, KEYCODE_TO_UNICODE_FR_FR, 128};
