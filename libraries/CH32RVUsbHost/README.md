# CH32RVUsbHost — the chip's own USB as a host

[日本語](README.ja.md)

Keyboards, mice and other HID, USB serial (CDC-ACM and FTDI / CP210x / CH34x /
PL2303), MIDI, mass storage, vendor bulk interfaces and CCID smart-card
readers, through a hub too. The API is EspUsbHost's (the same author's ESP32
library), renamed EspUsbHost -> CH32RVUsbHost.

Runs on CH32X035 R8T6 / C8T6 / G8U6 / G8R6 (the F8U6, F7P6, D8U6 and the X033
have no host in silicon; `begin()` says Unsupported), CH32V203/V208 (PB6/PB7)
and CH32V303/V305/V307/V317, CH32V205, CH32L103/M103, CH32M030 and CH32H41x
(PA11/PA12; L103/M103 need a CPU Clock of 48, 72 or 96 MHz - the default
96 MHz is one). Verified on a CH32X035C8T6 against an ESP32-S3 (HID, CDC, MSC, MIDI,
vendor, CCID); the others are build-only. The board
supplies VBUS. Not here: isochronous audio / video and USB networking.

The host stack is this library's own (CH32RVUsbHost.cpp, no TinyUSB), and so
is the host controller driver (CH32RVUsbHcdUsbfs.cpp behind CH32RVUsbHcd.h): every open endpoint can have a transfer pending, NAKs are retried
at the next frame (interrupt endpoints at their interval), STALL / no response
/ toggle errors are told apart, control transfers give up after 5 s. See
README.ja.md for the full description, and tests/peer/usb/host for the peer
tests against an ESP32-S3.

## Getting started

Start with [DeviceInfo](examples/DeviceInfo/README.md). The
[shared example guide](examples/README.md) covers exact parts, D+/D− wiring,
VBUS, the UART monitor, building and troubleshooting. Every example directory
has English and Japanese instructions matching its actual sketch.

```cpp
#include <CH32RVUsbHost.h>

CH32RVUsbHost usb;

void setup() {
  Serial.begin(115200);
  usb.onDeviceConnected([](const CH32RVUsbHostDeviceInfo &d) {
    usb.printDeviceInfo(d.address);
  });
  if (!usb.begin()) {
    Serial.println(usb.lastErrorName());
  }
}
void loop() {}
```

`Serial` is the hardware UART at 115200 baud, not the hosted USB port.
The board supplies 5-V VBUS through its own power circuit. Host startup does
not configure a Type-C source/CC circuit or switch a board's external supply.
X035F8U6 is a device-only part; choose a host-capable part such as C8T6.
V30x host uses USBFS here, even when the chip also contains USBHS.

## Lifecycle and callbacks

| API | Meaning |
|---|---|
| begin / end | Start/stop the controller and stack; check return values |
| ready | Controller started; does not mean a class has enumerated |
| task | Advance USB processing inside long application loops |
| onDeviceConnected / onDeviceDisconnected | Track addresses and connection state |
| deviceCount / getDevices / getDevice | Inspect connected devices |

Callbacks run in host task context after loop and during delay, not in the
ISR. Blocking APIs drive processing while waiting. Keep callbacks short;
several examples set a flag and do class open/transfer work from loop.
Callbacks are plain function pointers, one slot each. A new registration
replaces the previous callback. Use captureless lambdas or ordinary functions.
Copy callback data/string pointers into your own storage if needed later.

ANY_ADDRESS/ANY_PORT select the first matching device/port. For multiple
devices use callback addresses and explicit API arguments, or CDC
setAddress/setPort. CDC port numbers are zero-based within a device. Addresses
can change after reconnect.

## Descriptors and control transfers

| API | Purpose |
|---|---|
| printDeviceInfo / printAllDeviceInfo | Print identity, configuration, interfaces and endpoints |
| getInterfaces / getEndpoints | Enumerate descriptor information |
| getStringDescriptor | Read a string as ASCII; non-ASCII becomes ? |
| vendorControlTransfer | Specify request type, recipient, value, index and buffer |
| vendorControlIn / vendorControlOut | Convenience vendor-device requests |
| getHubInfo / getHubPortStatus / setHubPortPower | Hub capabilities, status/change and power requests |

Device address, interface number, endpoint address and CDC port are different
identifiers. Endpoint addresses include direction, such as 0x81. Use the
actual interface number in wIndex for interface requests and set the matching
recipient/direction. Hub power requests control a hub port; they do not switch
the board's own VBUS. Hub hardware may not implement individual switching.

## Class APIs

| Class | Main APIs |
|---|---|
| Keyboard | onKeyboard, setKeyboardLayout, setKeyboardLeds |
| Other HID | onMouse, onGamepad, onConsumerControl, onSystemControl |
| Raw/vendor HID | onHIDInput, onHIDVendorInput, onHIDReportDescriptor, sendHIDReport/getHIDReport, sendHIDVendorOutput/Feature, sendSetProtocol |
| Serial | CH32RVUsbHostCdcSerial Stream, or onSerialData/sendSerial |
| MIDI | onMidiMessage; note, control, program, pressure, pitch-bend and SysEx helpers |
| MSC | mscWaitReady, mscInquiry, mscCapacity, mscReadBlocks/WriteBlocks, mscRequestSense, mscSelectLun |
| Vendor bulk | vendorOpen/Close, vendorWrite, vendorAvailable/Read, onVendorData, getVendorInterface |
| CCID | ccidOpen, ccidPowerOn, ccidApdu, ccidEscape, card-state callbacks |

Keyboard ascii=0 can simply mean the key has no ASCII character. Use raw
usage and pressed/released state for non-text keys. Layout selection changes
host-side conversion, not the physical keyboard. Unusual gamepad/mouse fields
may need inspection of raw reports and descriptors.

The CDC Stream offers begin, available, read, peek, write, flush,
setBaudRate/setConfig and setDtr/setRts. begin before connection stores settings
for when a device appears. Check write return counts for partial transfers.
serialReady/serialPortCount/getSerialPortInfo expose class state. Baud is the
peer UART line coding, not USB bus speed.

When its receive ring fills, this implementation stops taking packets (the
device is NAKed). Do not assume EspUsbHost's old-byte-drop policy. End-to-end losses still
depend on the peripheral/UART and how often the application reads.

MIDI channel numbers are zero-based. Messages need a sound generator to
produce audio. MSC operates on blocks, without filesystem/mount support;
size buffers using blockCount × blockSize from the media, not a fixed 512-byte
assumption. Keep media ownership explicit when adding writes.

Vendor bulk is a byte stream without application message boundaries. One
vendor interface and one CCID interface can be open at a time. CCID APDU
response data excludes SW1/SW2, returned separately as statusWord, but buffer
capacity must also accommodate those two status bytes. Transport success and
APDU command success are distinct.

Exact signatures and structures are in the [public header](src/CH32RVUsbHost.h).

## Limits

Fixed counts: 5 devices (a hub and what is behind it), 2 hubs, 4 HID
interfaces, 2 USB serial ports, 1 mass storage, 1 MIDI. Configuration
descriptors are read up to 256 bytes. Receive buffers have defaults and
take the sketch's own arrays instead (no build-time settings):

| Default | Resource | Replace with |
|---|---|---|
| 128 | Stream receive ring | CH32RVUsbHostCdcSerial::setBuffer() |
| 256 | Vendor receive ring | setVendorBuffer(), before vendorOpen() |
| 272 | CCID ring and response | setCcidBuffers(), before ccidOpen() |

Report IDs kept per HID interface: 8; one string descriptor: 32 characters.

## Errors and EspUsbHost differences

Errors use CH32RVUsbHostError, not esp_err_t. Check lastErrorName after a
failed operation: Unsupported (part/implementation), Busy (device owns the
controller), NotStarted/NotConnected, NotSupported (function absent/not open),
Timeout, Stalled, TransferFailed, InvalidArgument, InvalidResponse or NoMemory.

This core uses its own host stack, function-pointer callbacks and
cooperative task processing. It does not provide ESP32 task/PHY/channel configuration,
listener lists, MSC filesystem mounting, host ISO audio/video or USB
networking. One hub level is supported. Sharing API vocabulary does not mean
all current EspUsbHost examples and features carry over.

## Examples and verification

| Example | Purpose |
|---|---|
| [DeviceInfo](examples/DeviceInfo/README.md) | Enumeration and descriptors |
| [Keyboard](examples/Keyboard/README.md) / [Mouse](examples/Mouse/README.md) | HID input and LEDs |
| [USBSerial](examples/USBSerial/README.md) | UART/USB serial bridge |
| [MIDI](examples/MIDI/README.md) | MIDI input and generated note |
| [MSCBlockDump](examples/MSCBlockDump/README.md) | Capacity and LBA 0 read |
| [VendorBulk](examples/VendorBulk/README.md) | A known bulk protocol peer |
| [CcidReader](examples/CcidReader/README.md) | ATR and APDU status |

See the [shared example guide](examples/README.md) and
[peer-test documentation](../../tests/peer/usb/README.ja.md). Build profiles
are not hardware-verification claims. Empty storage readers have a known
enumeration TEST UNIT READY retry limitation; insert media before connecting.
