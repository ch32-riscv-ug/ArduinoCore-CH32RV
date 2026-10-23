# USB device examples

[日本語](README.ja.md)

Connect the board as a USB device to a PC or another host. Start with [SerialUSBEcho](SerialUSBEcho/README.md), then choose a class. Each example explains wiring, operation, expected results, API use and adaptation notes.

## Preparation and first checks

1. Check the exact part, controller and clock requirements in the [USB support table (Japanese)](../../../docs/usb.ja.md#対応状況). A series-wide build does not validate every package or hardware configuration.
2. Use the connector wired to the MCU's USB data pins and a data-capable cable. Distinguish it from a connector wired only to a USB-UART bridge.
3. In Arduino IDE, choose the board and exact Part Number, then File → Examples → CH32RVUsbDevice. Use a working upload route for the initial upload.
4. Open an application for the class: CDC, HID, audio and so on. Not every class appears as a serial port.

With the core installed, compile from the repository root:

```sh
arduino-cli compile --fqbn ch32-riscv-ug:ch32rv:CH32X035:pnum=CH32X035F8U6 \
  libraries/CH32RVUsbDevice/examples/SerialUSBEcho
```

For development checks, confirm CLI resolves this working tree. See [example build rules (Japanese)](../../../docs/examples-build-rules.ja.md) for `sketch.yaml` and memory conditions.

## Keep UART diagnostics separate from USB communication

Most examples log on hardware `Serial`, separately from CDC `SerialUSB`/`UsbSerial`. Match the settings in `Serial.begin(...)`.

| Board | UART adapter |
|---|---|
| TX | RX |
| RX | TX, when commands are accepted |
| GND | GND |

Match I/O voltage and resolve aliases such as RX/TX through the board/variant and schematic. UART can report failures even when USB will not start. Examples without UART say so in their individual guides.

## From declaration to communication

Declare the device and its functions with sufficient lifetime, configure them, then start the device:

```cpp
#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceCdcSerial console(device, "Console");

void setup() {
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  CH32RVUsbDeviceConfig config;
  config.product = "My USB console";
  if (!device.begin(config)) {
    Serial.println(device.lastErrorName());
  }
}

void loop() {
  while (console.available()) {
    const int value = console.read();
    if (value >= 0) console.write(static_cast<uint8_t>(value));
  }
}
```

Attach functions to the same `device`. See [SerialUSBEcho](SerialUSBEcho/README.md) for predefined `USBDevice`/`SerialUSB`, and [CustomDevice](CustomDevice/README.md) for composition. Keep identity strings, custom descriptors and replacement buffers valid while USB uses them.

| State/API | Meaning |
|---|---|
| `device.begin(config)` returns `true` | Internal startup succeeded; host enumeration need not be complete |
| `device.started()` | The stack has started |
| `device.mounted()` / `device.ready()` | The host configured the device; these mean the same thing for Device |
| CDC `connected()` / boolean conversion | Host DTR is active, commonly used to check whether a terminal opened the port |
| A successful send API | The API accepted data; it does not prove the PC application interpreted or processed it |

CDC `begin(baud)` does not change USB bus speed. A bridge sets hardware UART baud separately. `write()` sends bytes; `print()` formats values as text. Waiting and return conventions differ between CDC, Vendor and Audio writes.

## Internal processing and callbacks

USB processing runs after `loop()` and during `delay()`. `device.task()` can advance it during long calculations, but the usual approach is to split work and return regularly.

| Kind | Examples | Execution and use |
|---|---|---|
| Notification | CDC receive, HID Output, bus events, video completion | Runs inside `deliverEvents()` called by `loop()`; some notifications coalesce to latest state |
| Response provider or decision | MSC block/START STOP handlers, CCID APDU, vendor control, video payload generation, DFU detach decision | Runs inside USB processing while an answer is needed; keep it short, without `delay()` or waiting loops |
| Polling | CDC `available()`, MIDI `readPacket()`, Audio `pollEvent()` | Read explicitly; polling alone does not require notification delivery |

Pending storage is finite. Depending on the class, the peer may wait, notifications may coalesce, or observation records may be dropped. An `onRx()` amount is not a message boundary. Use actual read lengths, and copy callback data if it must outlive the callback.

[SerialEvents](SerialEvents/README.md) demonstrates this flow. The [sketch composition guide (Japanese)](../../../docs/sketch-guide.ja.md) explains the execution model.

## Adapting examples

The sample VID/PID `1209:0001` is a test identity. Use identifiers appropriate for your product rather than shipping that test identity.

- Set identity in `CH32RVUsbDeviceConfig`. Use appropriate identifiers for a product and distinguish multiple boards by serial number.
- Complete descriptor-affecting configuration before `begin()`. Check host re-enumeration after changes.
- Pair HID presses with releases and MIDI Note On with Note Off, including failure/retry behavior.
- CDC and bulk are streams. Define message boundaries using delimiters or length fields.
- Check write results for audio, video and continuous data; retain unsent bytes or count discarded data deliberately.
- Respect host write caches for MSC. Do not modify a host-mounted medium simultaneously from the sketch.

## Troubleshooting

| Symptom | Check |
|---|---|
| No UART log | Pins, baud, GND, reset, and UART versus USB CDC |
| `begin()` fails | `lastErrorName()`, [library guide](../README.md), part/controller/clock and resource conflicts |
| Not visible on PC | Cable, data connector, D+/D− and selected controller |
| No CDC output | Correct port opened with DTR, and output not routed to UART |
| Callback does not run | Call `deliverEvents()` in `loop()` for registered notifications |
| Enumerates but cannot be used | Class application, report/PCM format, interface and endpoint selection |
| Loss only under load | Return values, long callbacks, read frequency and traffic volume |

Update entry and actual programming method depend on the series. See [USB upload](../../../docs/usb.ja.md#書き込み) and [upload routes](../../../docs/upload-routes.ja.md), both in Japanese. Avoid unintentionally triggering 1200-bps touch during normal CDC tests.

## Examples

| Example | Purpose |
|---|---|
| [AudioHeadset](AudioHeadset/README.md) | A UAC1 playback/capture loopback returning PC playback PCM through the recording stream. |
| [AudioMicrophone](AudioMicrophone/README.md) | A UAC1 capture source generating a 440-Hz test tone instead of using a physical microphone. |
| [AudioSpeaker](AudioSpeaker/README.md) | A UAC1 playback sink counting received PCM bytes on UART; it does not drive a speaker or DAC. |
| [CompositeHidCdcMsc](CompositeHidCdcMsc/README.md) | A composite keyboard, CDC port and ten-KiB FAT RAM disk, with keyboard input commanded over CDC. |
| [CustomDevice](CustomDevice/README.md) | A configurable composite device combining CDC, DFU runtime and Vendor HID, with a HID output-to-input echo. |
| [CustomHID](CustomHID/README.md) | A custom report descriptor defining an eight-byte input-only HID report. |
| [DescriptorDump](DescriptorDump/README.md) | Prints the descriptors the host is shown (device, configuration, BOS, MS OS 2.0, strings, HID) and every control request and bus event on the UART. |
| [DfuRuntime](DfuRuntime/README.md) | A CDC-free DFU runtime upload entry. Holding PA1 low makes `onDetach()` refuse the transition. |
| [Gamepad](Gamepad/README.md) | Generated HID gamepad input with six axes, a hat and a button bitmap. |
| [Keyboard](Keyboard/README.md) | A USB keyboard sending text, JIS symbols and a raw HID usage, while receiving Lock LED output reports. |
| [KeyboardMouse](KeyboardMouse/README.md) | Keyboard and mouse functions sharing one HID interface, distinguished by report ID. |
| [KeyboardNKRO](KeyboardNKRO/README.md) | An NKRO bitmap report holding ten keys plus Left Shift, with six-key reports retained for boot protocol. |
| [MIDI](MIDI/README.md) | Generated USB MIDI 1.0 notes and control changes, with received packets logged on UART. |
| [MIDIController](MIDIController/README.md) | A potentiometer controlling CC74 and a button controlling Note 60 over USB MIDI. |
| [MIDIInterface](MIDIInterface/README.md) | A bidirectional bridge between USB MIDI and 31250-baud UART MIDI messages. |
| [MSC](MSC/README.md) | An eight-KiB RAM-backed raw block device for SCSI and block-transfer checks. |
| [MSCFatRamDisk](MSCFatRamDisk/README.md) | A ten-KiB FAT12 RAM disk that reads back a CONFIG.TXT file written by the PC. |
| [MSCSdCard](MSCSdCard/README.md) | An SPI SD card exposed as USB mass storage; the PC owns the filesystem. |
| [MediaKeys](MediaKeys/README.md) | Consumer media-key reports plus a declared System Control function. |
| [Mouse](Mouse/README.md) | A USB mouse generating relative movement, a wheel event and a left-button press/release without external sensors. |
| [SerialEvents](SerialEvents/README.md) | Where the USB callbacks run: only in `deliverEvents()`. Data sent while `loop()` is busy waits and arrives in one call. |
| [Serial](Serial/README.md) | An explicitly declared CDC port with UART diagnostics for line coding, DTR/RTS and received data. |
| [SerialMulti](SerialMulti/README.md) | Two independent CDC ports, Console and Data Link, on one USB device. |
| [SerialUSBEcho](SerialUSBEcho/README.md) | A minimal CDC ACM device using the predefined `SerialUSB`. It echoes received bytes and sends a counter once a second while the terminal is open. |
| [SmartCardReader](SmartCardReader/README.md) | A one-slot CCID reader whose card is emulated in software; no physical card interface is needed. |
| [USBVendor](USBVendor/README.md) | A non-HID vendor interface with bulk IN/OUT, EP0 vendor requests and WebUSB/WinUSB descriptors. |
| [UsbNetworkPing](UsbNetworkPing/README.md) | A CDC-NCM device answering ARP and IPv4 ping directly from Ethernet frames, without an IP stack, web server or Internet connection. |
| [VendorHID](VendorHID/README.md) | Vendor-defined HID input, output and feature reports, distinct from the bulk-based USBVendor example. |
| [VideoCamera](VideoCamera/README.md) | A sensor-free UVC device generating YUY2 color bars and a moving white block. |
