/* Keyboard layout en_us: linked only when a sketch names it. The table is
 * EspUsbHost's keymap/keymap_en_us.h, byte for byte. */
#include "CH32RVUsbHost.h"
#include "keymap/keymap_en_us.h"

const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_EN_US = {
    0x0409, KEYCODE_TO_UNICODE_EN_US, 128};
