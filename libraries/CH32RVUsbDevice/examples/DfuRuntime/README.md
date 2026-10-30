# DfuRuntime

日本語: [README.ja.md](README.ja.md)

A CDC-free DFU runtime upload entry. Holding PA1 low makes `onDetach()` refuse the transition.

## Hardware and connections

A CH32X035/X033 with the implemented USB ISP entry, a PC and optionally a PA1-to-GND button. There is no UART log.

## Steps and expected results

1. Start the sketch and locate its DFU runtime interface.
2. Leave PA1 released and run `dfu-util -d 1209:0001 -e`.
3. On X035/X033 the application disconnects and the WCH ISP identity `1a86:55e0` appears.
4. After returning to the sketch, hold PA1 low and repeat: the application should remain active.

## Settings and limits

The steps above check the factory-ISP path. For another series, check the transition destination and programming method in the USB guide, along with host permissions and WinUSB binding.

DFU runtime is attached by default; the explicit object installs the veto callback. Some series enter a self-programming DFU mode instead. Consult the USB guide for coverage.

## API usage and adaptation

| API | Use in this example |
|---|---|
| `CH32RVUsbDeviceDfuRuntime dfu` | Adds a DFU function on predefined `USBDevice` with a configurable transition decision. |
| `dfu.onDetach(callback)` | Return `true` to allow transition or `false` to remain in the application. This is a decision, not an after-the-fact notification. A refusal stalls the DFU_DETACH request, so the host tool sees it fail and the device keeps running. |
| `USBDevice.begin(config)` | Exposes DFU without a CDC serial port. |

The detach callback runs inside USB request processing. Keep it to a short state check, such as whether a motor or write operation is active. Separate the decision from cleanup so refusal does not change application state.

Runtime is the interface for entering an update path. Whether it enters factory ISP or DFU mode, and which tool writes firmware, depends on the series. Follow the [USB upload guide (Japanese)](../../../../docs/usb.ja.md#書き込み); runtime enumeration alone does not imply `dfu-util -D` support.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[DfuRuntime.ino](DfuRuntime.ino)
