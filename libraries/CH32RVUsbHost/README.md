# CH32RVUsbHost — the chip's own USB as a host

[日本語](README.ja.md)

Keyboards, mice and other HID, USB serial (CDC-ACM and FTDI / CP210x / CH34x /
PL2303), MIDI, mass storage, vendor bulk interfaces and smart card readers
(CCID), through a hub too.

The API is that of EspUsbHost, the same author's ESP32 library, with
`EspUsbHost` -> `CH32RVUsbHost` and `ESP_USB_HOST_` -> `CH32RV_USB_HOST_`.

```cpp
#include <CH32RVUsbHost.h>

CH32RVUsbHost usb;

void setup() {
  Serial.begin(115200);
  usb.onDeviceConnected([](const CH32RVUsbHostDeviceInfo &d) { usb.printDeviceInfo(d.address); });
  usb.onKeyboard([](const CH32RVUsbHostKeyboardEvent &e) { if (e.pressed && e.ascii) Serial.print((char)e.ascii); });
  if (!usb.begin()) {
    Serial.println(usb.lastErrorName());
  }
}
void loop() { deliverEvents(); }   // the callbacks run here; the stack runs after loop() and inside delay()
```

## Support

| Series | Port | Checked |
|---|---|---|
| CH32X035 R8T6 / C8T6 / G8U6 / G8R6 | USBFS (PC16 / PC17) | on a C8T6 against an ESP32-S3 (EspUsbDevice): HID, CDC, MSC, MIDI, vendor, CCID |
| CH32X035 F8U6 / F7P6 / D8U6, CH32X033 | - (no host in silicon; `begin()` says `Unsupported`) | `Unsupported` seen on an F8U6 |
| CH32V203 / V208 | USBFS (PB6 / PB7) | built only |
| CH32V303 / V305 / V307 / V317 | USBFS (PA11 / PA12) | built only |
| CH32V205, CH32M030, CH32H415 / H416 / H417 | USBFS (PA11 / PA12). V205 and H41x: USBHS with `CH32RV_USB_HOST_PORT_HIGH_SPEED` (H41x: PB8 / PB9, shared with SWD) | built only. 48 MHz is made as for the device (V205 and H41x from the USBHS PLL, M030 from the 144 MHz PLL) |
| CH32V407 / V467 | USBHS1 (default, `HIGH_SPEED`), USBHS2 (`HIGH_SPEED_2`) | on a V407's USBHS1 against an ESP32-P4 (EspUsbDevice, high speed): HID, CDC, MSC, MIDI, vendor, CCID |
| CH32X305 / X315 | USBHS (default) | built only |
| CH32L103 / M103 | USBFS (PA11 / PA12) | built only. The 8 MHz CPU Clock cannot make 48 MHz and `begin()` fails (choose 48 / 72 / 96 MHz; not checked) |
| other series | - (`begin()` says `Unsupported`) | built only |

Which X035 parts have a host is the datasheet's part table, and `begin()`
checks it on the chip: whether BASE_CTRL's host-mode bit sets (it does not on
an F8U6). The peer tests are in [tests/peer/usb/host](../../tests/peer/usb/host/);
both (usb-peer-host / usb-peer-host-classes) pass on a CH32X035C8T6 against an
ESP32-S3, and on a V407 against an ESP32-P4 at high speed.

**The board supplies VBUS.** The chip cannot switch it. Starting the host does
not set up a Type-C source's CC circuit or switch the board's external supply.
On V30x the host uses USBFS even where the chip also has USBHS (no driver for
the first-generation USBHS host yet).

### Ports and speed

`begin()` starts on the series' default port; `CH32RVUsbHostConfig` picks
another (EspUsbHost's `EspUsbHostConfig.port`):

```cpp
CH32RVUsbHostConfig config;
config.port = CH32RV_USB_HOST_PORT_HIGH_SPEED;   // DEFAULT / HIGH_SPEED / FULL_SPEED / HIGH_SPEED_2 (V407 / V467's USBHS2)
usb.begin(config);                               // a port the part lacks: Unsupported
```

On the second-generation USBHS (V407 / V467, X305 / X315, V205, H41x) a
high-speed device runs at high speed, and full- and low-speed devices at
theirs when attached directly; `CH32RVUsbHostDeviceInfo::speed` is
`CH32RV_USB_HOST_SPEED_HIGH` (2). The receive buffers hold a 512-byte packet
(`CH32RV_USB_HOST_MAX_PACKET`; the default rings grow with it).

- High speed needs the crystal (HSE); the driver starts it and keeps HSI only
  on a board without one (then only short packets get through).
- Full- and low-speed devices behind a high-speed hub (split transactions) are
  not handled yet; a high-speed hub and high-speed devices behind it should
  be, but that is not checked on hardware.
- X305 / X315 without HSE can only trim HSI to a host's SOF, so a host there
  needs HSE.
- NYET after an OUT counts as accepted, and no PING is sent (the device NAKs
  until it has room).

`Serial` in the examples is the board's hardware UART (115200 baud), not the
hosted USB port.

The parts, D+/D−, VBUS, the UART wiring and building are in the
[shared example guide](examples/README.md). Check the first connection with
[DeviceInfo](examples/DeviceInfo/README.md).

## What it does

- **Devices**: connect / disconnect callbacks, descriptors, strings,
  interfaces, endpoints, control transfers (`vendorControlTransfer` /
  `vendorControlIn` / `vendorControlOut`), hubs (one level, port status and
  power).
- **HID**: keyboards (16 layouts, the Caps / Num / Scroll Lock LEDs), mice
  (fields placed from the report descriptor), consumer / system control,
  gamepads, vendor reports, raw input, report descriptors, output / feature
  reports, boot / report protocol.
- **CDC serial**: `CH32RVUsbHostCdcSerial` (a Stream) or `sendSerial` /
  `onSerialData`. Baud rate, data format, DTR / RTS.
- **MIDI**: a receive callback; sending notes, CC, program, pressure, pitch
  bend and SysEx.
- **Mass storage**: INQUIRY, capacity, REQUEST SENSE, TEST UNIT READY, block
  reads and writes, LUN selection. No file system (blocks only).
- **Vendor (bulk)**: open with `vendorOpen`, then `vendorWrite` / `vendorRead`
  / `onVendorData`.
- **CCID**: power, ATR, APDU (the response's SW comes apart in `statusWord`,
  as in EspUsbHost), escape, slot state, card insertion and removal.

## Differences from EspUsbHost

- Callbacks are function pointers (no `std::function`, no lists of
  listeners), for a chip with 20 KB of RAM.
- There are no task settings. The stack runs after `loop()` and inside
  `delay()`, and the waiting APIs run it while they wait. Callbacks are called
  from `deliverEvents()`, which the sketch calls (never inside `delay()` or an
  interrupt), and may call the waiting APIs. Until then reports, received data
  and packets wait in the stack's buffers (a full one holds the device off),
  and up to 4 connections and disconnections queue.
- No isochronous transfers (audio, video) and no USB networking: the chip has
  neither the bandwidth nor the buffers.
- MSC works in blocks only (nothing like `mscMount` / `EspUsbHostMscFS`).
- One vendor bulk interface and one CCID interface can be open at a time.
- USB serial has no `setRxBufferSize()`. Replace the receive buffer with the
  sketch's own array through `CH32RVUsbHostCdcSerial::setBuffer()` ("Sizes and
  errors" below).
- Errors are `CH32RVUsbHostError`, not `esp_err_t`. ESP32's task / PHY /
  channel settings do not carry over.

Sharing the API's vocabulary does not mean every feature of the current
EspUsbHost is here. The [public header](src/CH32RVUsbHost.h) in this repository
is what defines the API.

## Lifecycle and callbacks

| API | Meaning |
|---|---|
| `begin()` / `end()` | Start / stop the controller and the host stack. Check the result and `lastErrorName()` |
| `ready()` | The host has started; not that a device has enumerated |
| `task()` | Moves the host along explicitly, inside long computations |
| `onDeviceConnected()` / `onDeviceDisconnected()` | A device connected / disconnected |
| `deviceCount()` / `getDevices()` / `getDevice()` | The connected devices and their addresses |

Each callback is one slot holding a function pointer; registering again
replaces it. Use lambdas without captures or plain functions, and keep the
state you need in static variables. Copy an event's `data` / `rawData` and
string pointers into your own buffer to use them later.

`CH32RV_USB_HOST_ANY_ADDRESS` and `CH32RV_USB_HOST_ANY_PORT` pick the first
matching device / port. A USB address can change across a reconnect. With
several devices, choose by the address the connect callback gives, and drop
the state on disconnect. A CDC port is a number from 0 within one device.

## Descriptors and control transfers

| API | For |
|---|---|
| `printDeviceInfo(address)` / `printAllDeviceInfo()` | Identity, configuration, interfaces and endpoints to a `Print` (`Serial` by default) |
| `getInterfaces()` / `getEndpoints()` | Interfaces / endpoints from the descriptors |
| `getStringDescriptor(address, index, out, capacity)` | A string; non-ASCII becomes `?` |
| `vendorControlTransfer(requestType, request, value, index, ...)` | An EP0 transfer with its request type and recipient |
| `vendorControlIn()` / `vendorControlOut()` | The short forms for vendor device requests |
| `getHubInfo()` / `getHubPortStatus()` / `setHubPortPower()` | A hub's information, port status / change, port power |

For your own interface request, put the descriptor's interface number in
`wIndex` and give the right recipient and direction. The USB address, the
interface number, the endpoint address and the CDC port number are different
values; an endpoint address such as `0x81` includes the direction. Read the
actual descriptors rather than reusing fixed values from a PC application or
another implementation.

The hub power API is a hub's port power request, not a switch of the board's
own VBUS, and a hub may not implement per-port power.

## Class APIs

### HID

| API | For |
|---|---|
| `onKeyboard()` / `setKeyboardLayout()` / `setKeyboardLeds()` | Keys, the character layout, the Lock LEDs |
| `onMouse()` / `onGamepad()` | Input decoded from the descriptor |
| `onConsumerControl()` / `onSystemControl()` | Media and system usages |
| `onHIDInput()` / `onHIDVendorInput()` / `onHIDReportDescriptor()` | Raw input, vendor input, the report descriptor |
| `sendHIDReport()` / `getHIDReport()` | Report transfers naming the interface, type and report ID |
| `sendHIDVendorOutput()` / `sendHIDVendorFeature()` | Sending vendor reports |
| `sendSetProtocol()` | Switching between boot and report protocol |

A keyboard event's `ascii=0` also means a key that has no ASCII character;
the raw usage and pressed / released handle keys other than characters. A
gamepad's own layout is checked through the raw report and the descriptor.

### USB serial

`CH32RVUsbHostCdcSerial port(usb)` is an Arduino `Stream`: `begin(baud)`,
`available()`, `read()`, `peek()`, `write()`, `flush()`, with `setBaudRate()` /
`setConfig()` / `setDtr()` / `setRts()` for the settings. A `begin()` before a
device is there keeps the settings and applies them when one appears. Choose
among several ports with `setAddress()` / `setPort()`.

The callback form is `onSerialData()` / `sendSerial()`; the state is
`serialReady()` / `serialPortCount()` / `getSerialPortInfo()`. The baud setting
is the peer UART's line coding, not the speed of the FS bus. For large
transfers, check what `write()` returns and move the resend position on.

When the receive ring is full, this implementation takes no more packets (the
device is held off with NAK). Do not apply EspUsbHost's "the old data is
dropped" description. Whether anything is lost in the end also depends on the
peer device, its UART and how often the sketch reads.

### MIDI

`onMidiMessage()` receives the cable, status and data1 / data2.
`midiSendNoteOn/Off()`, `midiSendControlChange()` and the program / pressure /
pitch bend / SysEx helpers send. Channels count from 0. The arguments are in
the [public header](src/CH32RVUsbHost.h) and the
[MIDI example](examples/MIDI/README.md).

### MSC

Go `mscWaitReady()` -> `mscInquiry()` / `mscCapacity()` -> `mscReadBlocks()` /
`mscWriteBlocks()`. The bytes needed are block count × block size; a medium
does not always have 512-byte sectors, so read the capacity.
`mscRequestSense()` gives the SCSI state and `mscSelectLun()` picks the LUN.
A reader with no medium in it is mounted without a capacity after a few
TEST UNIT READY tries; `mscWaitReady()` waits longer. There are no file
systems, file names or mounting. Before adding writes, be clear who owns the
medium, so as not to break its partitions or FAT.

### Vendor bulk and CCID

`vendorOpen()` opens a class `0xff` interface; `vendorWrite()`, `vendorRead()`
/ `onVendorData()` talk through it. `getVendorInterface()` shows the actual
endpoints. The bulk byte stream has no application message boundaries.

CCID has `ccidOpen()`, `ccidPowerOn()`, `ccidApdu()`, `ccidEscape()` and card
state notifications. `ccidApdu()`'s response is the payload without SW1/SW2,
and the status word is a separate output, but the receive buffer's capacity
must hold those two status bytes too. A successful transfer and a successful
command (`SW=9000`) are different things.

## Sizes and errors

The counts are fixed: 5 devices (a hub and what is behind it), 2 hubs, 4 HID
interfaces, 2 USB serial ports, 1 MSC, 1 MIDI. Configuration descriptors are
read up to 256 bytes (interfaces beyond that are not assigned).

The receive buffers have default sizes and take the sketch's own arrays
instead (there are no build-time settings).

| Default | Buffer | Replace with |
|---|---|---|
| 128 | the USB serial (Stream) receive ring | `CH32RVUsbHostCdcSerial::setBuffer()` |
| 256 | the vendor receive ring | `setVendorBuffer()` (before `vendorOpen()`) |
| 272 | the CCID receive ring and response | `setCcidBuffers()` (before `ccidOpen()`) |

Each HID interface remembers 8 report IDs; a string descriptor is up to 32
characters, the terminator included.

| Error | Meaning |
|---|---|
| `Unsupported` | No host in this implementation, or none in the silicon |
| `Busy` | The device side has the controller |
| `NotStarted` / `NotConnected` | Not started / no such device |
| `NotSupported` | The function is not there, or the interface is not open |
| `Timeout` / `Stalled` / `TransferFailed` | Out of time / refused by the device / the transfer failed |
| `InvalidArgument` / `InvalidResponse` | A bad argument / a bad response |
| `NoMemory` | A buffer or table is full |

## Examples

The wiring, steps, output and limits are in each README, from the
[example index](examples/README.md).

| Example | What it checks |
|---|---|
| [DeviceInfo](examples/DeviceInfo/README.md) | Enumeration, interfaces / endpoints, plugging and unplugging |
| [Keyboard](examples/Keyboard/README.md) / [Mouse](examples/Mouse/README.md) | HID input and the LEDs |
| [USBSerial](examples/USBSerial/README.md) | Relaying between the UART and USB serial |
| [MIDI](examples/MIDI/README.md) | MIDI input and a periodic note |
| [MSCBlockDump](examples/MSCBlockDump/README.md) | The medium's capacity and reading LBA 0 |
| [VendorBulk](examples/VendorBulk/README.md) | Sending and receiving a known bulk protocol |
| [CcidReader](examples/CcidReader/README.md) | The ATR and an APDU's status word |

## Inside

- The host stack is this library's own (`CH32RVUsbHost.cpp`; no TinyUSB). The
  interrupt only records completed transfers and port changes; the control
  transfer stages, enumeration (the root port and hubs) and the class work run
  in normal context. The host controller driver (`CH32RVUsbHcdUsbfs.cpp`, the
  table in `internal/CH32RVUsbHcd.h`) holds a transfer per open endpoint and runs them in
  turn through the single transaction engine. NAKs are retried at the next
  frame (interrupt endpoints at their bInterval), STALL, no response and toggle
  errors are told apart, and a control transfer gives up after 5 s (the USB
  spec's limit).
- Only the files of the functions a sketch uses are linked (the hub always is).
- The device (CH32RVUsbDevice) uses the same controller, so only one of them
  runs at a time (the other is `Busy`).
