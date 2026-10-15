/* Keyboard layout en_gb: linked only when a sketch names it. The table is
 * EspUsbHost's keymap/keymap_en_gb.h, byte for byte. */
#include "CH32RVUsbHost.h"
#include "keymap/keymap_en_gb.h"

const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_EN_GB = {
    0x0809, KEYCODE_TO_UNICODE_EN_GB, 128};
