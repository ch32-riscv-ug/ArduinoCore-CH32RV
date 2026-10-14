# CH32RVUsbDevice — the chip's own USB as a device

CDC serial, HID (keyboard, mouse, gamepad, media keys, system control, vendor,
custom), MIDI and a DFU runtime interface (an upload entry), combined by
declaring them. The API follows
[EspUsbDevice](https://github.com/tanakamasayuki/EspUsbDevice): rename `Esp`
to `CH32RV` and most sketches carry over. Built on the vendored TinyUSB
([CH32RVTinyUSB](../CH32RVTinyUSB/)). The full guide (Japanese) is
[docs/usb.ja.md](../../docs/usb.ja.md).

```cpp
#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceHidKeyboard keyboard(device);

void setup() {
  keyboard.setLayout(CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_JA_JP);
  CH32RVUsbDeviceConfig config;
  config.product = "My keyboard";
  if (!device.begin(config)) {
    // device.lastErrorName() says why
  }
}
void loop() {
  keyboard.write("Hello @\n");
  delay(3000);
}
```

`USBDevice` and `SerialUSB` are predefined like `Serial`, so `SerialUSB.begin()`
alone works too.

## Coming from EspUsbDevice

| EspUsbDevice | CH32RVUsbDevice |
|---|---|
| `#include "EspUsbDevice.h"` | `#include <CH32RVUsbDevice.h>` |
| `EspUsbDevice`, `EspUsbDeviceHidKeyboard`, ... | `CH32RVUsbDevice`, `CH32RVUsbDeviceHidKeyboard`, ... |
| `ESP_USB_HID_KEY_A`, `ESP_USB_DEVICE_MOD_LEFT_SHIFT`, ... | `CH32RV_USB_HID_KEY_A`, `CH32RV_USB_DEVICE_MOD_LEFT_SHIFT`, ... |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_JA_JP` (an enum) | `CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_JA_JP` (a table object; same call) |
| `EspUsbDeviceConfig::controller` | `CH32RVUsbDeviceConfig::controller` (`CH32RVUsbController`) |
| `lastError()` returns `esp_err_t` | `lastError()` returns `CH32RVUsbDeviceError`; `lastErrorName()` as before |

Differences:
- **Callbacks are function pointers** (EspUsbDevice takes `std::function`): a
  lambda without captures converts, one with captures does not.
- **Only the keyboard layouts a sketch names are linked**; all 19 together
  would be about 17 KB.
- **CDC is a `Stream`**, and blocks until everything is written while a host
  has the port open (EspUsbDevice returns what fit). It also takes the
  1200 bps touch.
- **The stack runs from `loop()` and `delay()`**, not an RTOS task;
  `device.task()` runs it explicitly.
- **Counts and buffers are fixed at build time**: two CDC ports need
  `-DCFG_TUD_CDC=2` in the sketch's `build_opt.h`.
- **Not yet**: vendor (bulk), MSC, CCID, audio, video, USB networking, the
  firmware-update classes, WebUSB / MS OS 2.0 config fields.
- **A DFU runtime interface comes by default** (EspUsbDevice has none), so
  any USB sketch can be re-uploaded over USB; `config.dfuRuntime = false`
  leaves it out.
- **Only here**: `CH32RVUsbDeviceDfuRuntime` (the ISP entry), predefined
  `SerialUSB` / `USBDevice`, `enterBootloader()`.

## Classes

`CH32RVUsbDevice`, `CH32RVUsbDeviceCdcSerial`, `CH32RVUsbDeviceHidKeyboard`
(layouts, NKRO, boot protocol, `ledState()`), `CH32RVUsbDeviceHidMouse`,
`CH32RVUsbDeviceHidGamepad`, `CH32RVUsbDeviceHidConsumerControl`,
`CH32RVUsbDeviceHidSystemControl`, `CH32RVUsbDeviceHidVendor`,
`CH32RVUsbDeviceHidCustom`, `CH32RVUsbDeviceMidi` (up to 16 cables),
`CH32RVUsbDeviceDfuRuntime` (a DFU runtime interface only: DFU_DETACH resets
into the factory USB ISP bootloader; no DFU mode, no DFU download).

All HID classes of a device share one HID interface, as in EspUsbDevice: one
class alone stays as it is (a keyboard or mouse alone is a boot device), two or
more are told apart by report ID.

## Examples

Serial, SerialMulti, Keyboard, KeyboardNKRO, Mouse, KeyboardMouse, MediaKeys,
Gamepad, VendorHID, CustomHID, MIDI, MIDIController, MIDIInterface - ported
from EspUsbDevice's examples of the same name with only the renames, pins and
VID/PID changed (the HID ones send input on their own once running, as the
originals do). SerialUSBEcho, CustomDevice and DfuRuntime are this library's.

## Support

CH32X035 / X033 (USBFS), checked on a WeAct CH32X035F8U6 board. On other
series `begin()` returns false (`Unsupported`) and the sketch still builds.

## Uploading

A 1200 bps touch (with a CDC port) or a DFU_DETACH (with
`CH32RVUsbDeviceDfuRuntime`) resets into the factory USB ISP bootloader
(1a86:55e0), where `ch32rv isp flash --entry auto` writes the chip. DFU is
only the entry; the write is WCH's ISP protocol, not a DFU download.
