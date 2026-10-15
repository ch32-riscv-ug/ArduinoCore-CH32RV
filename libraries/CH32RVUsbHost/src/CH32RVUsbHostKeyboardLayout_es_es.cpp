/* Keyboard layout es_es: linked only when a sketch names it. The table is
 * EspUsbHost's keymap/keymap_es_es.h, byte for byte. */
#include "CH32RVUsbHost.h"
#include "keymap/keymap_es_es.h"

const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_ES_ES = {
    0x0C0A, KEYCODE_TO_UNICODE_ES_ES, 128};
