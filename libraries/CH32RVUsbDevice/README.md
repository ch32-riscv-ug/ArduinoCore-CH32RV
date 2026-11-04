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

### Differences

- **Callbacks are `CH32RVCallback`** (EspUsbDevice takes `std::function`): a
  function pointer, or a lambda capturing at most two pointers' worth
  (`[this]`, a pointer and a small value). No heap; a bigger capture is a
  compile error.
- **Only the keyboard layouts a sketch names are linked**; all 19 together
  would be about 17 KB.
- **CDC is a `Stream`**, and blocks until everything is written while a host
  has the port open - up to `setTxTimeoutMs()` (1000 ms) without progress,
  then the rest is dropped (EspUsbDevice does the same, but sends without a
  host that has the port open too). How this library
  follows EspUsbDevice v3 is in [docs/usb-espusbdevice-v3.ja.md](../../docs/usb-espusbdevice-v3.ja.md). It also takes the
  1200 bps touch.
- **The stack runs from `loop()` and `delay()`**, not an RTOS task;
  `device.task()` runs it explicitly.
- **Notification callbacks run from `deliverEvents()`** (`onRx()`,
  `onLineState()`, `onBusEvent()`, `onFrame()` ...), which the sketch calls
  in `loop()`; never inside `delay()`. Until then the data waits in the
  class's buffer or the host. Providers the host waits on (`onRead()` /
  `onWrite()` of MSC, `onApdu()`, `onControlRequest()`, `onFillPayload()`)
  are called inside the stack.
- **Each class has a count limit** (CDC and MIDI 4, vendor and video 2,
  others 1); within it, declaring is enough. CDC and vendor buffers are set
  with `configure(CH32RVUsbBulkStreamConfig)` (EspUsbDevice v3's field names):
  the class's own by default, the sketch's array when given; nothing is
  allocated. CCID (`CH32RVUsbCcidConfig`) and video (`CH32RVUsbVideoConfig`)
  are set with `configure()` too. Neither the number of classes nor the size
  of the descriptors has a build-time limit.
- **Disks are objects the MSC class reads from** (`setStorage()`; `attach()`
  does it), not lambdas bound to `onRead()`.
- **The network's IP layer** (`ipConfig()`, `dhcpServer()`, `dhcpClient()`,
  `dhcpAdvertiseGateway()`, `dhcpDns()`, `defaultRoute()`, `beginNetwork()`,
  `networkUp()`, `localIP()`) comes from the Network library: a sketch using it
  includes `<Network.h>` too. That library is for the series with an Ethernet
  MAC (V208, V307/V317, V407/V467, H41x), not the X035. Without
  `beginNetwork()` the function exchanges raw Ethernet frames. Check controller
  and networking hardware coverage in the [USB support table (Japanese)](../../docs/usb.ja.md#対応状況).
- **Not here**: the firmware-update classes ([docs/usb.ja.md](../../docs/usb.ja.md#確かめていない構成と無いもの), Japanese).
- **The CDC and vendor classes' direct path (`writeDirect()`, from their shared base `CH32RVUsbDeviceBulkStream`) works on every series**:
  one transfer straight from the caller's buffer, which stays untouched until
  `onDirectTxComplete()` hands it back (once, whether sent, reset or stopped).
  No build flag and no alignment rule (the USBSS DMA reads a 4-byte-aligned
  buffer itself). Receiving can borrow too (`rxMode` Borrowed, `readDirect()`),
  and either loan can be cancelled (`cancelDirectTx()` / `cancelDirectRx()`).
  `onRxData()` is a run-time choice; its transfers land in the receive buffer
  `configure()` gives. Only here:
  `setInterfaceProtocol()` (subclass / protocol) and the HID vendor class's
  `setUsage()`.
- **A DFU runtime interface comes by default**; `config.dfuRuntime = false`
  leaves it out. The transition and programming method depend on the series:
  factory ISP, self-programming DFU mode, or a normal reset. See
  [USB uploading (Japanese)](../../docs/usb.ja.md#書き込み).
- **The descriptor getters have the same names** (`deviceDescriptor()`,
  `configurationDescriptor()`, `stringDescriptor()`, `bosDescriptor()`,
  `microsoftOs20Descriptor()`, `hidReportDescriptor()` and the rest). Nothing
  is kept resident: configuration, BOS, MS OS 2.0 and HID report getters use
  shared heap storage. Consume or copy the result before the next descriptor-building
  call, including a different getter. String pointers last until the next
  `stringDescriptor()` call. `onAnyControlRequest()`
  behaves the same (vendor requests at SETUP, every control transfer at ACK).
- **Only here**: `CH32RVUsbDeviceDfuRuntime` (the ISP entry), predefined
  `SerialUSB` / `USBDevice`, `enterBootloader()`, `onBusEvent()` (reset,
  configured, unconfigured, suspended, resumed),
  `configurationDescriptorFor(CH32RVUsbSpeed)`.

## Classes

| Class | What it is |
|---|---|
| `CH32RVUsbDevice` | The device. `begin(config)`, `end()`, `started()` / `ready()`, `remoteWakeupEnabled()` / `remoteWakeup()` (declared with `config.remoteWakeupSupported`), `task()`, `sendHidReport()`, `lastErrorName()`, `enterBootloader()` |
| `CH32RVUsbDeviceCdcSerial` | A virtual COM port (`Stream`). `connected()`, `lineCoding()`, `lineState()`, `onLineCoding()`, `onLineState()`, `onRx()`, the 1200 bps touch. `configure()`, `onRxData()`, `onTxComplete()`, `writeDirect()` / `readDirect()` and the rest are shared with Vendor (`CH32RVUsbDeviceBulkStream`) |
| `CH32RVUsbDeviceHidKeyboard` | A keyboard. `write()`, `tapKey()`, `tapUsage()`, `pressUsage()`, `releaseAll()`, `setLayout()`, `ledState()`, `enableNkro()` |
| `CH32RVUsbDeviceHidMouse` | A mouse. `move()`, `wheel()`, `click()`, `press()` / `release()` |
| `CH32RVUsbDeviceHidGamepad` | A gamepad (6 axes, a hat, 32 buttons). `send()`, `sendReport()` |
| `CH32RVUsbDeviceHidConsumerControl` | Media keys. `click(CH32RV_USB_DEVICE_CONSUMER_CONTROL_VOLUME_UP)` and so on |
| `CH32RVUsbDeviceHidSystemControl` | Power off, standby, waking the host |
| `CH32RVUsbDeviceHidVendor` | A vendor-defined HID (data without a driver). `sendInput()`, `onOutputReport()`, `onFeatureReport()`, `setUsage()`. Reports up to 63 bytes on the full-speed-only series, 511 / 1023 bytes on the HS / SS series |
| `CH32RVUsbDeviceHidCustom` | A HID with your own report descriptor |
| `CH32RVUsbDeviceMidi` | USB MIDI (up to 16 cables). `noteOn(channel, note, velocity, cable)`, `readPacket()`, `configure(CH32RVUsbMidiConfig)` (queue sizes, how events are taken and sent), `onPacket()` |
| `CH32RVUsbDeviceVendor` | Vendor-defined bulk transfers (`Print`). `read()`, `peek()` (this core's own), `write()`, `flush()`, `configure()`, `waitWritable()`, `onRx()`, `onControlRequest()`, `writeDirect()`, `onDirectTxComplete()`, `readDirect()`, `onDirectRxComplete()`, `cancelDirectTx()`, `cancelDirectRx()`, `onTxComplete()`, `onRxData()`, `setInterfaceProtocol()`. WebUSB and Windows' WinUSB are on while this class is there |
| `CH32RVUsbDeviceMsc` | A USB drive. `begin(blocks, size)`, `onRead()` / `onWrite()`, `mediaPresent()` (a medium put in while running is reported to the host as changed), `isWritable()`, `vendorID()` and so on |
| `CH32RVUsbDeviceMscRamDisk` / `CH32RVUsbDeviceMscFatRamDisk` | Blocks in RAM / a FAT12 volume in RAM. `attach(msc)`, `addTextFile()`, `readFile()`, `onEject()` |
| `CH32RVUsbDeviceCcid` | A smart card reader. `insertCard(atr)`, `removeCard()`, `onApdu()`, `onEscape()`, `onPower()`. `messageBytes` 0, the default, is 271 bytes or the chosen controller's bulk OUT packet (512 on USBHS, 1024 on USBSS), whichever is larger |
| `CH32RVUsbDeviceMscSdCard` | An SPI SD card as the MSC's medium. `begin(cs, SPI)`, `attach(msc)` (`#include <CH32RVUsbDeviceMscSdCard.h>`) |
| `CH32RVUsbDeviceNet` | A USB network adapter (CDC-NCM). `onFrame()`, `sendFrame()`, `macAddress()`, `linkUp()`, `configure()` (NTB sizes and buffer counts), `stats()`, `receiveGate()` / `resumeReceive()` for receive flow control. The IP layer: above |
| `CH32RVUsbAudioFunction` | Audio (UAC1 / UAC2). `addPlaybackStream()` / `addCaptureStream()`, `addFormat()` (up to 8 rates of one shape), `read()` / `write()`, mute and volume, `pollEvent()`. A full playback ring drops the oldest frames unless `playbackOverflowPolicy` says otherwise. The capture side's `availableForWrite()` is this library's addition |
| `CH32RVUsbDeviceVideo` | A camera (UVC). `setFormat()`, `setFrameSize()`, `sendFrame()`, `onCommit()`. `onFillPayload()` / `sendGeneratedFrame()` (making the frame without holding it) are this library's additions |
| `CH32RVUsbDeviceDfuRuntime` | An update entry through DFU_DETACH; the next mode depends on the series. **Included by default** (`config.dfuRuntime = false` leaves it out); declare it to decide the transition in `onDetach()`. Its interface name, "CH32RV", marks a ch32rv upload port in discovery |

Every default string and ID (VID/PID, manufacturer, product, interface names,
MSC inquiry, FAT volume label) is changed at run time - CH32RVUsbDeviceConfig,
setName(), vendorID() and the like - not from build_opt.h. Nothing is limited
at build time either: descriptors are produced as the host reads them.

All HID classes of a device share one HID interface, as in EspUsbDevice: one
class alone stays as it is (a keyboard or mouse alone is a boot device), two or
more are told apart by report ID.

## Examples

| Example | What it is |
|---|---|
| Serial, SerialMulti | CDC. SerialMulti has two |
| Keyboard, KeyboardNKRO, Mouse, KeyboardMouse, MediaKeys, Gamepad | HID. **They send input by themselves once started** (as EspUsbDevice's do) |
| VendorHID, CustomHID | A vendor HID / your own report descriptor |
| MIDI, MIDIController, MIDIInterface | MIDI. MIDIInterface converts between UART MIDI and USB MIDI |
| USBVendor | Vendor bulk with WebUSB and vendor requests |
| MSC, MSCFatRamDisk | A RAM disk / a FAT RAM disk (cut to 8-10 KB for the X035's RAM) |
| CompositeHidCdcMsc | Keyboard + CDC + MSC in one device |
| SmartCardReader | A CCID card reader |
| MSCSdCard | An SD card as a USB drive |
| AudioSpeaker, AudioMicrophone, AudioHeadset | Audio (16-32 kHz, for the X035's 64 B/ms) |
| VideoCamera | A camera sending 80x60 colour bars |
| UsbNetworkPing | Answers ARP and ping over the USB network (this library's own) |
| SerialUSBEcho, CustomDevice, DfuRuntime | This library's own: `SerialUSB` alone, the settings and their failures, the upload entry alone |

The [complete example index](examples/README.md) gives the hardware, operation,
expected output, the constants to change and the limits of each sketch. The
examples taken from EspUsbDevice differ too: disks sized for CH32's RAM, audio
sample rates, video without a frame buffer.

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

`begin()` returning true means the stack started; `ready()` means the host
configured the device; a CDC port's `connected()` means DTR is set. In a long
loop of your own, call `device.task()` to keep the stack running.

## Checking each class from the host

| Class | Host-side check |
|---|---|
| CDC | Serial terminal with DTR; baud is not bus speed |
| HID | Input window or raw report viewer; distinguish ID from payload |
| MIDI | MIDI monitor/synthesizer; API channels are zero-based |
| MSC | MSC for raw blocks, MSCFatRamDisk for files, MSCFileSystem for a volume the sketch also mounts (FS library) |
| Vendor | libusb/WinUSB/WebUSB application, not a serial terminal |
| CCID | PC/SC ATR and APDU; device responses include SW1/SW2 |
| Audio | Playback/recording with the advertised PCM format |
| Video | UVC viewer supporting the resolution, YUY2 and transport |
| NCM | A NIC and static IP; IP handled by the sketch (raw frames) or the Network library (`beginNetwork()`) |

A callback's receive buffer is not yours to keep: copy what is needed later,
and do not rely on long waits or re-entry inside device callbacks. `loop()` and
`delay()` move USB along, and notification callbacks arrive in
`deliverEvents()`. Ignoring the length `write()` returns for audio or video can
lose data.

### Example-specific limits

- The HID examples that type and click by themselves send characters, clicks and volume changes once connected; have a test target ready.
- [MIDIController](examples/MIDIController/README.md) maps 12-bit readings while the core defaults to 10 bits; its README explains how to use the full CC range.
- AudioSpeaker only counts what it receives, AudioMicrophone generates a tone, and AudioHeadset loops USB back. None drives a real codec.
- VideoCamera generates an 80×60 test pattern per payload. With ISO on X035, lower the frame rate too.
- UsbNetworkPing is for experiments with small, ordinary ARP/ICMP packets: no DHCP, web serving or general IP parser.

## Support

See the [USB support table (Japanese)](../../docs/usb.ja.md#対応状況) for implemented controllers, exact-part exceptions, pins, clocks and hardware verification. Compilation and hardware validation are separate checks.

## Uploading

CDC 1200-bps touch and DFU_DETACH provide update entry points. The destination and programming method depend on the series; follow the [USB upload guide (Japanese)](../../docs/usb.ja.md#書き込み).
`onDetach()` makes an immediate transition decision and does not wait for notification delivery through `deliverEvents()`. Returning false stalls the DFU_DETACH request, so the host sees it fail. Once accepted, the device leaves whatever follows - `end()` (which then does not return), a bus reset, SET_CONFIGURATION 0. The request's wValue and the descriptor's wDetachTimeOut (1000 ms) are not used: the device detaches itself (WILL_DETACH). EspUsbDevice v3 keeps the same contract.
