# USB device examples

[日本語](README.ja.md)

These examples connect the board as a device to a PC or another USB host. Start with [SerialUSBEcho](SerialUSBEcho/README.md), then choose a class below. Each sketch has its own wiring, operation, output and limitations guide.

## Boards and wiring

The device hardware baseline is a WeAct CH32X035F8U6. Only X035/X033 has a controller driver for now; the V103/V20x/V30x rows are the wiring once their drivers return; consult the [support table](../../../docs/usb.ja.md#対応状況) for part-specific exceptions. H417 SuperSpeed is not currently supported by these examples.

| Controller | D− | D+ | Selection |
|---|---|---|---|
| X035/X033 USBFS | PC16 | PC17 | Auto |
| V103/V303 USBFS | PA11 | PA12 | Auto |
| V203/V208 USBFS | PB6 | PB7 | FullSpeed, or normally Auto |
| V203/V208 USBD | PA11 | PA12 | Usbd; Auto on USBD-only parts |
| V305/V307/V317 USBFS | PA11 | PA12 | FullSpeed, or normally Auto |
| V305/V307/V317 USBHS | PB6 | PB7 | HighSpeed; requires HSE |

Check the exact part/package and board schematic. Use a USB data cable on the connector wired to the MCU controller; a connector wired only to a USB-UART bridge will not enumerate the device stack.

Most examples log through hardware `Serial` at 115200 baud, 8N1. Connect board TX→adapter RX, board RX→adapter TX for commands, and GND→GND, using the board I/O voltage. Board RX/TX/SCK aliases depend on the selected variant. This UART is separate from USB CDC `SerialUSB`/`UsbSerial`. SerialUSBEcho and DfuRuntime do not use UART.

## Build and upload

In Arduino IDE, choose the board and exact Part Number, then open File → Examples → CH32RVUsbDevice. Initially upload through a known working route before testing the running application's USB upload entry.

From the repository root, with the core installed:

```sh
arduino-cli compile --fqbn ch32-riscv-ug:ch32rv:CH32X035:pnum=CH32X035F8U6 \
  libraries/CH32RVUsbDevice/examples/SerialUSBEcho
```

For development-core checks, verify that CLI resolves this working tree. Generated sketch profiles pin a distributed platform version; they are not a hardware-validation matrix.

- Audio profiles require at least 48 KiB flash; RAM-disk profiles require at least 16 KiB RAM. Actual fit depends on the selected part and linked image.
- `pnum=ANY` declares the series minimum memory. Select the actual part when using its larger RAM/flash.
- Do not hand-edit generated `sketch.yaml`; see [example build rules](../../../docs/examples-build-rules.ja.md).

## Runtime and configuration

Declare functions on one device, then call `device.begin(config)` once, or use predefined USBDevice. HID functions share one HID interface; DFU runtime is attached by default without consuming an endpoint.

`begin()` means the stack started. `mounted()` means the host configured it. CDC `connected()`/boolean state means DTR is active, so open a terminal with DTR before expecting periodic CDC output.

| Setting | Where to change it |
|---|---|
| VID/PID, product, serial | CH32RVUsbDeviceConfig in the sketch; 1209:0001 is a test ID |
| Multiple board identities | Unique serial per board, or nullptr for the chip-derived default |
| Controller | config.controller plus matching physical pins and HSE for HS |
| Counts/buffers | Per-class limits (CDC and MIDI 4, vendor and video 2, others 1); CDC buffers via setBuffers() |
| Keyboard characters | setLayout plus the PC keyboard layout |
| Audio/video format | The example's sample/frame settings and controller bandwidth |

The USB stack runs after loop and during delay. Call device.task regularly in long processing loops. Notification callbacks (onRx, onLineState, onBusEvent ...) run only from deliverEvents(), which loop() calls; until then the data waits. Providers the host waits on (MSC onRead/onWrite, CCID onApdu, vendor onControlRequest) run inside the stack: keep them short and without delay. [SerialEvents](SerialEvents/README.md) shows the difference; the [guide (Japanese)](../../../docs/sketch-guide.ja.md) explains it.

## Troubleshooting and re-upload

| Symptom | Check |
|---|---|
| No UART log | UART wiring, 115200 baud, GND, monitor timing; do not confuse UART with CDC |
| Unsupported | Implemented series/controller support |
| PortUnavailable | Exact part, controller pins, HSE and CH32RV_HSE_HZ |
| TooManyInstances | More of one class than its limit |
| OutOfEndpoints / DescriptorTooLarge | Composite function and descriptor limits |
| NotReady / failed HID report | Host configuration and suspend state |
| No CDC data | DTR, terminal local echo and newline settings |
| No enumeration | Data cable, correct connector, D+/D− and controller selection |

HID, MSC, audio and other non-CDC classes do not appear as serial ports. Inspect interfaces with a USB descriptor viewer and open them through the appropriate class application.

On X035/X033, 1200-bps touch or DFU_DETACH enters WCH ISP. Avoid 1200 baud during normal terminal tests. DFU runtime does not implement DFU firmware download. Other series currently reset normally instead of entering ISP. See [USB upload](../../../docs/usb.ja.md#書き込み) and [upload routes](../../../docs/upload-routes.ja.md).

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
