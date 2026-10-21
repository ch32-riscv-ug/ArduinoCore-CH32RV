# CH32RVUsbDevice — the chip's own USB as a device

[日本語](README.ja.md)

CDC serial, HID (keyboard, mouse, gamepad, media keys, system control, vendor,
custom), MIDI, vendor bulk (with WebUSB and the Microsoft OS 2.0 descriptors),
mass storage (RAM disks, an SD card), a smart card reader (CCID), a network
adapter (CDC-NCM), audio (UAC1/UAC2), a camera (UVC) and a DFU runtime
interface (an upload entry), combined by declaring them. The API follows
[EspUsbDevice](https://github.com/tanakamasayuki/EspUsbDevice); migration also
requires checking API differences, pins, RAM, endpoints and bandwidth below. Built on its own device stack, not
TinyUSB. The full guide (Japanese) is
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

Start with [SerialUSBEcho](examples/SerialUSBEcho/README.md). The
[example guide](examples/README.md) covers wiring, building, UART versus USB
CDC, and troubleshooting. Every example has English and Japanese instructions.

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
- **Callbacks are `CH32RVCallback`** (EspUsbDevice takes `std::function`): a
  function pointer, or a lambda capturing at most two pointers' worth
  (`[this]`, a pointer and a small value). No heap; a bigger capture is a
  compile error.
- **Only the keyboard layouts a sketch names are linked**; all 19 together
  would be about 17 KB.
- **CDC is a `Stream`**, and blocks until everything is written while a host
  has the port open (EspUsbDevice returns what fit). It also takes the
  1200 bps touch.
- **The stack runs from `loop()` and `delay()`**, not an RTOS task;
  `device.task()` runs it explicitly.
- **Each class has a count limit** (CDC and MIDI 4, vendor and video 2,
  others 1); within it, declaring is enough. CDC buffers can be replaced with
  the sketch's own through `setBuffers()`.
- **Disks are objects the MSC class reads from** (`setStorage()`; `attach()`
  does it), not lambdas bound to `onRead()`.
- **The network's IP layer** (`ipConfig()`, `dhcpServer()`, `dhcpClient()`,
  `dhcpAdvertiseGateway()`, `dhcpDns()`, `defaultRoute()`, `beginNetwork()`,
  `networkUp()`, `localIP()`) comes from the Network library: a sketch using it
  includes `<Network.h>` too. That library is for the series with an Ethernet
  MAC (V208, V307/V317, V407/V467, H41x), not the X035. Without
  `beginNetwork()` the function exchanges raw Ethernet frames. Those series
  have no USB device driver yet, so this is tested on the host only
  (`tests/unit/network`).
- **Not here**: the firmware-update classes.
- **The vendor class's direct path (`writeDirect()`) works on every series**:
  one transfer straight from the caller's buffer, which stays untouched until
  `onTxComplete()` reports it. No build flag and no alignment rule (the USBSS
  DMA reads a 4-byte-aligned buffer itself). `onRxData()` is a run-time choice
  too, and `setRxDataBuffer()` gives it a bigger landing buffer. Only here:
  `setInterfaceProtocol()` (subclass / protocol) and the HID vendor class's
  `setUsage()`.
- **A DFU runtime interface comes by default**; `config.dfuRuntime = false`
  leaves it out. It enters WCH ISP on X035/X033; other series currently reset
  normally. This differs from EspUsbDevice OTA/DFU download support.
- **The descriptor getters have the same names** (`deviceDescriptor()`,
  `configurationDescriptor()`, `stringDescriptor()`, `bosDescriptor()`,
  `microsoftOs20Descriptor()`, `hidReportDescriptor()` and the rest). Nothing
  is kept resident: each call builds into one shared heap buffer, and a
  pointer stays valid until the next such call. `onAnyControlRequest()`
  behaves the same (vendor requests at SETUP, every control transfer at ACK).
- **Only here**: `CH32RVUsbDeviceDfuRuntime` (the ISP entry), predefined
  `SerialUSB` / `USBDevice`, `enterBootloader()`, `onBusEvent()` (reset,
  configured, unconfigured, suspended, resumed),
  `configurationDescriptorFor(CH32RVUsbSpeed)`.

## Classes

`CH32RVUsbDevice`, `CH32RVUsbDeviceCdcSerial`, `CH32RVUsbDeviceHidKeyboard`
(layouts, NKRO, boot protocol, `ledState()`), `CH32RVUsbDeviceHidMouse`,
`CH32RVUsbDeviceHidGamepad`, `CH32RVUsbDeviceHidConsumerControl`,
`CH32RVUsbDeviceHidSystemControl`, `CH32RVUsbDeviceHidVendor`,
`CH32RVUsbDeviceHidCustom`, `CH32RVUsbDeviceMidi` (up to 16 cables),
`CH32RVUsbDeviceVendor` (bulk; with `peek()`, which EspUsbDevice lacks), `CH32RVUsbDeviceMsc` with
`CH32RVUsbDeviceMscRamDisk` / `CH32RVUsbDeviceMscFatRamDisk` /
`CH32RVUsbDeviceMscSdCard` (its own header), `CH32RVUsbDeviceCcid`,
`CH32RVUsbDeviceNet` (CDC-NCM, raw frames), `CH32RVUsbAudioFunction`
(UAC1/UAC2), `CH32RVUsbDeviceVideo` (UVC),
`CH32RVUsbDeviceDfuRuntime` (a DFU runtime interface only: DFU_DETACH resets
into the factory USB ISP bootloader; no DFU mode, no DFU download). Its
interface name, "CH32RV", is what ch32rv's discovery lists as an upload port.

Every default string and ID (VID/PID, manufacturer, product, interface names,
MSC inquiry, FAT volume label) is changed at run time - CH32RVUsbDeviceConfig,
setName(), vendorID() and the like - not from build_opt.h. Nothing is limited
at build time either: descriptors are produced as the host reads them.

All HID classes of a device share one HID interface, as in EspUsbDevice: one
class alone stays as it is (a keyboard or mouse alone is a boot device), two or
more are told apart by report ID.

## Examples

Serial, SerialMulti, Keyboard, KeyboardNKRO, Mouse, KeyboardMouse, MediaKeys,
Gamepad, VendorHID, CustomHID, MIDI, MIDIController, MIDIInterface, USBVendor,
MSC, MSCFatRamDisk, MSCSdCard, CompositeHidCdcMsc, SmartCardReader,
AudioSpeaker, AudioMicrophone, AudioHeadset and VideoCamera are adapted from
EspUsbDevice, with CH32-specific memory, audio-rate and generated-video changes.
HID examples generate input automatically. SerialUSBEcho, CustomDevice,
DfuRuntime and UsbNetworkPing are this library's.

The [complete example index](examples/README.md) links hardware, operation,
expected output, settings and limitations for each sketch.

## Composite configuration and runtime

Declare functions on the same device and call begin once after configuring
them. Declaration order affects interface/endpoint allocation. Multiple HID
functions share one HID interface with report IDs. Default DFU uses no endpoint.

| Configuration | Endpoint-number usage |
|---|---|
| Keyboard | One for HID |
| CDC | Two: notification and duplex data |
| Two CDC ports | Four |
| Keyboard + CDC + MSC | One + two + one = four |

Controllers differ in IN/OUT resources, packet memory and ISO limits. Do not
apply ESP32-specific endpoint counts to CH32. OutOfEndpoints and
TooManyInstances indicate different resource limits; see the
[controller table](../../docs/usb.ja.md#コントローラと速度).

begin success means the stack started; mounted means the host configured it.
CDC connected means DTR is active. Callbacks are small (no heap); copy
received data if needed later and keep device callbacks short. Tasks run after
loop and during delay; call device.task inside long processing loops.

## Choosing the matching host application

| Class | Host-side check |
|---|---|
| CDC | Serial terminal with DTR; baud is not bus speed |
| HID | Input window or raw report viewer; distinguish ID from payload |
| MIDI | MIDI monitor/synthesizer; API channels are zero-based |
| MSC | MSC for raw blocks, MSCFatRamDisk for files |
| Vendor | libusb/WinUSB/WebUSB application, not a serial terminal |
| CCID | PC/SC ATR and APDU; device responses include SW1/SW2 |
| Audio | Playback/recording with the advertised PCM format |
| Video | UVC viewer supporting the resolution, YUY2 and transport |
| NCM | A NIC and static IP; IP handled by the sketch (raw frames) or the Network library (`beginNetwork()`) |

The MIDIController sketch maps 12-bit readings while the core defaults to
10 bits; its README explains how to use the full CC range. AudioSpeaker counts
received PCM, AudioMicrophone generates a tone, and AudioHeadset loops PCM back;
none drives a physical codec. VideoCamera generates an 80×60 pattern per payload;
ISO on X035 needs a lower frame rate. UsbNetworkPing handles ordinary small
ARP/ICMP packets, without DHCP, web serving or a general IP parser.

## Support

CH32X035 / X033 (USBFS), checked on a WeAct CH32X035F8U6 board. The
controller drivers for V103, V20x and V30x are being rewritten for the own
stack (they existed in the TinyUSB days). Until then, on those and the
other series `begin()` returns false (`Unsupported`) and the sketch still
builds.

## Uploading

On X035/X033, a 1200 bps touch (with a CDC port) or a DFU_DETACH (with
`CH32RVUsbDeviceDfuRuntime`) resets into the factory USB ISP bootloader
(1a86:55e0), where `ch32rv isp flash --entry auto` writes the chip. DFU is
only the entry; the write is WCH's ISP protocol, not a DFU download. Other
series currently reset normally and require their supported upload route.
