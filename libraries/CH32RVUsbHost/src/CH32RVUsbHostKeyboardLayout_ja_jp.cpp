/* Keyboard layout ja_jp: linked only when a sketch names it. The table is
 * EspUsbHost's keymap/keymap_ja_jp.h, byte for byte. */
#include "CH32RVUsbHost.h"
#include "keymap/keymap_ja_jp.h"

const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_JA_JP = {
    0x0411, KEYCODE_TO_UNICODE_JA_JP, 0x90};
