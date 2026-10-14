/* Keyboard layout hu-hu: linked only when a sketch names it. The table is
 * EspUsbDevice's keymap/keymap_hu_hu.h, byte for byte. */
#include "CH32RVUsbDevice.h"
#include "keymap/keymap_hu_hu.h"

const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_HU_HU = {
    0x040E, KEYCODE_TO_UNICODE_HU_HU, 128};
