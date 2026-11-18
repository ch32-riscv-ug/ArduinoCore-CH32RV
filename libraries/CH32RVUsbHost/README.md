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
| CH32V103 | USB (the older USBFS IP, PA11 / PA12) | built only. CPU Clock 48 / 72 MHz (48 MHz from the PLL) |
| CH32V303 / V305 / V307 / V317 | USBFS (PA11 / PA12). V305 / V307 / V317: the first-generation USBHS with `CH32RV_USB_HOST_PORT_HIGH_SPEED` (PB6 / PB7, needs HSE) | built only |
| CH32V205, CH32M030, CH32H415 / H416 / H417 | USBFS (PA11 / PA12). V205 and H41x: USBHS with `CH32RV_USB_HOST_PORT_HIGH_SPEED` (H41x: PB8 / PB9, shared with SWD) | built only. 48 MHz is made as for the device (V205 and H41x from the USBHS PLL, M030 from the 144 MHz PLL) |
| CH32H416 / H417 (also) | USBSS with `CH32RV_USB_HOST_PORT_SUPER_SPEED`: USB 3 at 5 Gbit/s only | implemented, not yet tested on hardware |
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

### Ports and speed

`begin()` starts on the series' default port; `CH32RVUsbHostConfig` picks
another (EspUsbHost's `EspUsbHostConfig.port`):

```cpp
CH32RVUsbHostConfig config;
config.port = CH32RV_USB_HOST_PORT_HIGH_SPEED;   // DEFAULT / HIGH_SPEED / FULL_SPEED / HIGH_SPEED_2 (V407 / V467's USBHS2)
                                                 // / SUPER_SPEED (H416 / H417's USBSS)
usb.begin(config);                               // a port the part lacks: Unsupported
```

On the second-generation USBHS (V407 / V467, X305 / X315, V205, H41x) a
high-speed device runs at high speed, and full- and low-speed devices at
theirs when attached directly; `CH32RVUsbHostDeviceInfo::speed` is
`CH32RV_USB_HOST_SPEED_HIGH` (2). On the series whose default port is high
speed (V407 / V467, X305 / X315) the receive buffers hold a 512-byte packet
(`CH32RV_USB_HOST_MAX_PACKET`; the default rings grow with it). Where the
default is full speed (V205, H41x, V30x) they stay full speed's 64 bytes, so
a sketch on that port grows by nothing; only when a sketch picks the high-speed
(or H416 / H417's SuperSpeed) port does that port's driver take heap buffers
for endpoints with larger packets (CDC, MIDI, vendor, CCID; 1024 bytes at
SuperSpeed; HID reports stay within 64 bytes).

- High speed needs the crystal (HSE); the driver starts it and keeps HSI only
  on a board without one (then only short packets get through).
- Full- and low-speed devices behind a high-speed hub go through the hub's
  transaction translator by split transactions (the controller sends the
  SPLIT token, the driver runs the start and complete splits), with a single
  or a multi TT alike. Not there: CLEAR_TT_BUFFER after a transfer cut off in
  the hub, and isochronous OUT of more than 188 bytes. **Implemented, not yet
  tested on hardware.**
- H41x's USBHS is PB8 / PB9, the SWD pins: after `begin()` a debugger cannot
  attach (until a reset).
- X305 / X315 without HSE can only trim HSI to a host's SOF, so a host there
  needs HSE.
- NYET after an OUT counts as accepted, and no PING is sent (the device NAKs
  until it has room).

The first-generation USBHS (V305 / V307 / V317's `HIGH_SPEED`, PB6 / PB7) is a
high-speed host too. Its PHY's PLL comes from the crystal only, so it needs
HSE (the Clock Source menu's crystal; 8 MHz when HSE is not chosen there).
**Implemented, not yet tested on hardware.** Its SPLIT register is 12 bits
with no description of its fields, so full- and low-speed devices behind a
high-speed hub are not handled there (their endpoints do not open); a
low-speed device behind a full-speed hub (PRE) is not checked either. V30x's
class buffers stay full speed's 64 bytes (a sketch on USBFS alone grows by
nothing); an endpoint with 512-byte high-speed packets gets heap buffers when
it opens (CDC, MIDI, vendor, CCID; HID reports stay within 64 bytes).

On every port only the driver of the port a sketch picks is linked (begin()
picks it in the sketch; with a constant port the others stay out).

On H416 / H417, `SUPER_SPEED` runs the USBSS controller as a SuperSpeed
(USB 3, 5 Gbit/s) host. **Implemented, not yet tested on hardware.** It
takes SuperSpeed only: a USB 2.0 device, or the USB 2.0 half of a USB 3 hub,
on the same connector belongs to the USBHS controller (`HIGH_SPEED`), a host
of its own; the two do not run together yet. `speed` is
`CH32RV_USB_HOST_SPEED_SUPER` (3), and the USBSS driver takes heap buffers
for endpoints with 1024-byte packets (`CH32RV_USB_HOST_MAX_PACKET` stays full
speed's 64). The stack's SuperSpeed part (the BOS / SET_SEL enumeration, route
strings, SuperSpeed hubs, the 1024-byte descriptor windows a 512-byte EP0
needs) is linked only when a sketch picks `SUPER_SPEED` too: a sketch on the
full-speed port is as large as on any full-speed series (HID report
descriptors up to 256 bytes).

- Control, bulk, interrupt and isochronous (Mult included) endpoints, one
  packet per transaction for now; SuperSpeed hubs (route strings, hub depth);
  U1 / U2 accepted from the device after SET_SEL (build with
  `-DCH32RV_USB_HOST_SS_LINK_PM=0` to keep every link in U0).
- No streams: the controller has no Stream ID field, so a UAS interface does
  not work (a mass-storage device's BOT interface does).

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
- MSC's file system is `CH32RVUsbHostMscFS` (shaped as `EspUsbHostMscFS`, see MSC below). There is no ESP32 VFS, so no host `mscMount()`.
- One vendor bulk interface and one CCID interface can be open at a time.
- USB serial's receive ring is sized as EspUsbHost's, `setRxBufferSize(size)`
  (from the heap, before `begin()` or after `end()`, 66 .. 65535 bytes) and
  `rxBufferSize()`. Without the heap, `CH32RVUsbHostCdcSerial::setBuffer()`
  takes the sketch's own array ("Sizes and errors" below).
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

### Suspend and resume

| API | Meaning |
|---|---|
| `suspend()` | The root port suspends (no SOF; U3 on SuperSpeed), and with it the device there and everything behind a hub on it. Every device that offers remote wakeup in its configuration has it enabled first (`SET_FEATURE(DEVICE_REMOTE_WAKEUP)`) |
| `resume()` | The port drives the resume (K for 20 ms on USB 2.0) and transfers start again after the device's 10 ms recovery |
| `suspended()` | `suspend()` is in effect |
| `onRemoteWakeup()` | A device woke the bus: the port has already resumed |

Transfers asked for while suspended wait until the port resumes (a control
transfer meets its timeout). `suspend()` fails with `NotConnected` without a
device and `Unsupported` where the controller's driver cannot suspend.
Measured on V205's USBFS with an RP2040 and V407's USBHS with an ESP32-P4
(tests/peer/usb, usb-peer-host-power); V30x's USBHS and USBSS are build only.

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
TEST UNIT READY tries; `mscWaitReady()` waits longer. `mscEject()` sends
START STOP UNIT with LoEj (a firmware-update drive takes it as the end of the
image). `mscSynchronizeCache()` sends SYNCHRONIZE CACHE(10).

For files, use `CH32RVUsbHostMscFS` (`#include <CH32RVUsbHostMscFS.h>`, the
FS library's `FatFS`), shaped as EspUsbHost's `EspUsbHostMscFS`:

```cpp
#include <CH32RVUsbHost.h>
#include <CH32RVUsbHostMscFS.h>

CH32RVUsbHost usb;
CH32RVUsbHostMscFS stick;

void loop() {
  if (usb.mscReady() && !stick.mounted()) {
    stick.begin(usb);                       // "/usb", the first mass storage device, LUN 0
  }
  if (stick.mounted()) {
    File f = stick.open("/log.txt", FILE_APPEND);
    f.println(millis());
    f.close();
  }
  delay(1000);
}
```

- FAT12/16/32 on media with 512-byte blocks (exFAT where the FS library has
  it built in).
- `begin(host, basePath, address, lun, maxFiles, timeoutMs, skipSyncCache)`
  takes `basePath` and `maxFiles` and does not use them: paths are the
  volume's own (`"/log.txt"`), and the open files are the FS library's
  `FS_MAX_OPEN_FILES`.
- `sync()` and `end()` send SYNCHRONIZE CACHE (not with `skipSyncCache`); a
  drive without a cache refusing it is not a failure.
- A medium with no volume can get one with `stick.format(stick.device())`
  (where format is built in).
- `CH32RVUsbHostMscFS.h` is header only: a sketch that does not include it
  carries neither it nor the FS library.
- Once the stick is pulled, every call is false until `end()` and `begin()`
  again.

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
interfaces, 2 USB serial ports, 1 MSC, 1 MIDI. A configuration descriptor of
any length (up to the 65535 bytes USB allows) is read a window at a time - 256
bytes, 1024 on H416 / H417 where SuperSpeed's EP0 packet is 512 - fetched
again from the start with the bytes before the window dropped: no RAM grows
with the device. A class driver sees a window from its interface on, so one
function's descriptors (interface, class and endpoint descriptors together)
are read up to about 190 bytes (510 on H416 / H417); longer configurations
cost a few more control transfers at enumeration.

The receive buffers have default sizes and take the sketch's own arrays
instead (there are no build-time settings).

| Default | Buffer | Replace with |
|---|---|---|
| 128 | the USB serial (Stream) receive ring | `setRxBufferSize()` / `CH32RVUsbHostCdcSerial::setBuffer()` |
| 256 | the vendor receive ring | `setVendorBuffer()` (before `vendorOpen()`) |
| 272 | the CCID receive ring and response | `setCcidBuffers()` (before `ccidOpen()`) |

Each HID interface remembers 8 report IDs. The manufacturer, product and
serial strings are kept whole (up to the 126 characters a descriptor carries;
`CH32RV_USB_HOST_STRING_MAX` 127 with the terminator) in one pool shared by
the devices - 512 bytes, 1024 on the series with a high-speed host: one
device's three longest possible strings and the others' usual short ones.
Only a pool already full cuts a string.

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
