/* Keyboard layout nb-no: linked only when a sketch names it. The table is
 * EspUsbDevice's keymap/keymap_nb_no.h, byte for byte. */
#include "CH32RVUsbDevice.h"
#include "keymap/keymap_nb_no.h"

const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_NB_NO = {
    0x0414, KEYCODE_TO_UNICODE_NB_NO, 128};
