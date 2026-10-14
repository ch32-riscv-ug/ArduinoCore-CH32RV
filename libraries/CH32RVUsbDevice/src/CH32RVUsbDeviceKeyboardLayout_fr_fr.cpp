/* Keyboard layout fr-fr: linked only when a sketch names it. The table is
 * EspUsbDevice's keymap/keymap_fr_fr.h, byte for byte. */
#include "CH32RVUsbDevice.h"
#include "keymap/keymap_fr_fr.h"

const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_FR_FR = {
    0x040C, KEYCODE_TO_UNICODE_FR_FR, 128};
