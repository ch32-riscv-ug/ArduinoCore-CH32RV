/* Keyboard layout ja-jp: linked only when a sketch names it. The table is
 * EspUsbDevice's keymap/keymap_ja_jp.h, byte for byte. */
#include "CH32RVUsbDevice.h"
#include "keymap/keymap_ja_jp.h"

const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_JA_JP = {
    0x0411, KEYCODE_TO_UNICODE_JA_JP, 0x90};
