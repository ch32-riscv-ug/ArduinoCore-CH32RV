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

The runtime interface does not implement DFU_DNLOAD. Flash through the WCH ISP support in `ch32rv` after detaching. DFU runtime is normally attached by default; this explicit object installs the veto callback. Other series currently reset normally instead of entering their ISP. See the guide for host access permissions and driver binding.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[DfuRuntime.ino](DfuRuntime.ino)
