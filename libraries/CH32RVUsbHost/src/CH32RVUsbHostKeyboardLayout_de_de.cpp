/* Keyboard layout de_de: linked only when a sketch names it. The table is
 * EspUsbHost's keymap/keymap_de_de.h, byte for byte. */
#include "CH32RVUsbHost.h"
#include "keymap/keymap_de_de.h"

const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_DE_DE = {
    0x0407, KEYCODE_TO_UNICODE_DE_DE, 128};
