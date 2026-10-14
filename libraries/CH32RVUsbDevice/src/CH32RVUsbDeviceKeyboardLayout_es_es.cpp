/* Keyboard layout es-es: linked only when a sketch names it. The table is
 * EspUsbDevice's keymap/keymap_es_es.h, byte for byte. */
#include "CH32RVUsbDevice.h"
#include "keymap/keymap_es_es.h"

const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_ES_ES = {
    0x0C0A, KEYCODE_TO_UNICODE_ES_ES, 128};
