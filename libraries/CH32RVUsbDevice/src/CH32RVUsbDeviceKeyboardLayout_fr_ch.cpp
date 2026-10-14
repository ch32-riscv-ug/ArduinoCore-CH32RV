/* Keyboard layout fr-ch: linked only when a sketch names it. The table is
 * EspUsbDevice's keymap/keymap_fr_ch.h, byte for byte. */
#include "CH32RVUsbDevice.h"
#include "keymap/keymap_fr_ch.h"

const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_FR_CH = {
    0x100C, KEYCODE_TO_UNICODE_FR_CH, 128};
