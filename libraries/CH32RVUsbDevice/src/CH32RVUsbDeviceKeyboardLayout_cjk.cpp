/* Korean and Chinese layouts: their keyboards type ASCII where US ones do, so
 * they share the en-us table (as in EspUsbDevice). */
#include "CH32RVUsbDevice.h"
#include "keymap/keymap_en_us.h"

const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_KO_KR = {
    0x0412, KEYCODE_TO_UNICODE_EN_US, 128};

const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_ZH_CN = {
    0x0804, KEYCODE_TO_UNICODE_EN_US, 128};

const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_ZH_TW = {
    0x0404, KEYCODE_TO_UNICODE_EN_US, 128};
