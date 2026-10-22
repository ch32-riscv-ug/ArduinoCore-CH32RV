# USB host examples

[日本語](README.ja.md)

The board is the USB host; the PC observes its UART. Start with [DeviceInfo](DeviceInfo/README.md) to confirm enumeration, then select a class example. Connect a USB device to the board host data port, not another host.

## Parts and connections

| Part/controller | D− | D+ | Status |
|---|---|---|---|
| X035 R8T6/C8T6/G8U6/G8R6 | PC16 | PC17 | Host present; C8T6 peer-tested |
| X035 F8U6/F7P6/D8U6, X033 | — | — | No host in silicon; Unsupported |
| V203/V208 USBFS | PB6 | PB7 | Build-only |
| V303/V305/V307/V317 USBFS | PA11 | PA12 | Build-only |

The WeAct X035F8U6 device baseline cannot serve as the host baseline. Choose C8T6 or another host-capable part. V30x uses USBFS here, not USBHS. H417 USBSS/USBHS host is not implemented.

Wire D+/D− and GND, and provide 5-V VBUS to the device from the board power circuit/switch. begin does not create a supply or configure the Type-C power role. A device-only Type-C connector with sink Rd resistors does not by itself provide host/source wiring; check the board source/CC circuit.

Every example uses hardware Serial at 115200 baud, 8N1. Wire board TX→adapter RX and GND; board RX←adapter TX is also required for console input in USBSerial and VendorBulk. Use the board I/O voltage.

## Build and run

Choose the exact Part Number in Arduino IDE, then File → Examples → CH32RVUsbHost. From the repository root with the core installed:

```sh
arduino-cli compile --fqbn ch32-riscv-ug:ch32rv:CH32X035:pnum=CH32X035C8T6 \
  libraries/CH32RVUsbHost/examples/DeviceInfo
```

1. Upload with the peripheral disconnected and open the UART monitor.
2. Confirm VBUS and data wiring, then connect the peripheral.
3. Inspect its interfaces with DeviceInfo and choose the matching class example.
4. Check I/O, unplug and replug behavior with a matching device.

Generated sketch profiles declare build conditions, not host-capable hardware. A series profile can compile on a part without host support. See the [build rules](../../../docs/examples-build-rules.ja.md).

## Task processing and selection

The host stack runs after loop and during delay. Call usb.task in long processing loops. Callbacks run only from deliverEvents(), which each example calls at the top of loop(); they may use blocking APIs but should remain short. Several examples set a flag and open or transfer from loop. See the [guide (Japanese)](../../../docs/sketch-guide.ja.md).

usb.ready means the controller started. Use serialReady, midiReady, mscReady and class open results to check a function. ANY_ADDRESS selects the first matching device. With multiple devices use callback addresses and API address arguments, or CDC setAddress/setPort. Do not assume the same USB address survives reconnect.

One hub level is supported, with limits on device/interface/buffer counts. Host audio/video ISO and NCM/ECM networking are not implemented. EspUsbHost runtime configuration and examples do not all carry over.

## Troubleshooting

| Symptom | Check |
|---|---|
| Unsupported | Exact part, especially X035 F8U6 versus C8T6 |
| Busy | Active device stack on the same controller; end before switching |
| No connection event | VBUS, GND, D+/D−, device-mode port and UART logging |
| Identity printed, no class I/O | Class/protocol support, class readiness and open results |
| Timeout / Stalled | Missing response versus a peer refusing a request; inspect lastErrorName and descriptors |
| Large composite cannot enumerate | CFG_TUH_ENUMERATION_BUFSIZE and related limits, including RAM cost |
| Storage reader never ready | Insert media first; enumeration TEST UNIT READY has a known unlimited-retry limitation |
| Serial losses | Polling frequency, return values and compile-time ring/FIFO sizes |

See the [library API](../README.md) and [peer tests](../../../tests/peer/usb/README.ja.md) for support and verification boundaries.

## Examples

| Example | Purpose |
|---|---|
| [CcidReader](CcidReader/README.md) | A USB CCID reader example printing card ATR and a SELECT MF status word. |
| [DeviceInfo](DeviceInfo/README.md) | UART device identity, strings, interfaces and endpoints: the first check for host wiring. |
| [Keyboard](Keyboard/README.md) | USB keyboard ASCII input printed on UART, with Caps/Num Lock LED updates. |
| [MIDI](MIDI/README.md) | UART logging of USB MIDI messages, with periodic transmission of Note 60. |
| [MSCBlockDump](MSCBlockDump/README.md) | USB storage INQUIRY/capacity and a hex dump of the first block, without a filesystem. |
| [Mouse](Mouse/README.md) | UART inspection of USB mouse relative movement, wheel and button bitmap. |
| [USBSerial](USBSerial/README.md) | A bidirectional UART-console bridge to a CDC-ACM device or supported USB-UART adapter. |
| [VendorBulk](VendorBulk/README.md) | A vendor bulk interface bridge: UART input to bulk OUT, and received bulk IN shown as hex. |
