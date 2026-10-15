/* Keyboard layout fr_ch: linked only when a sketch names it. The table is
 * EspUsbHost's keymap/keymap_fr_ch.h, byte for byte. */
#include "CH32RVUsbHost.h"
#include "keymap/keymap_fr_ch.h"

const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_FR_CH = {
    0x100C, KEYCODE_TO_UNICODE_FR_CH, 128};
