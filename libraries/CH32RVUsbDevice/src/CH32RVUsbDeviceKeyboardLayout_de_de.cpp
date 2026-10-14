/* Keyboard layout de-de: linked only when a sketch names it. The table is
 * EspUsbDevice's keymap/keymap_de_de.h, byte for byte. */
#include "CH32RVUsbDevice.h"
#include "keymap/keymap_de_de.h"

const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_DE_DE = {
    0x0407, KEYCODE_TO_UNICODE_DE_DE, 128};
