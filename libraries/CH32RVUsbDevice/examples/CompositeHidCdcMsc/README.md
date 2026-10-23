# CompositeHidCdcMsc

日本語: [README.ja.md](README.ja.md)

A composite keyboard, CDC port and ten-KiB FAT RAM disk, with keyboard input commanded over CDC.

## Hardware and connections

A PC CDC terminal, text editor, file browser and UART monitor. Select a part with RAM for both the disk and USB buffers.

## Steps and expected results

1. Check that CDC, HID and the CH32RV volume appear together.
2. A normal CDC line returns as `echo: ...`.
3. Sending `type Hello` followed by a newline types Hello and Enter into the currently focused window.
4. An open CDC port receives a tick about every five seconds.

## Settings and limits

Focus a test editor before issuing type commands. The 128-byte command buffer retains at most 127 characters. The RAM disk resets with the board. HID, CDC and MSC use one, two and one endpoint numbers respectively; ESP32-specific IN endpoint limits do not apply to CH32.

## API usage and adaptation

| API | Use in this example |
|---|---|
| Each function's `...(device)` | Adds keyboard, CDC and MSC to one device. |
| `disk.format()` / `disk.addTextFile()` / `disk.attach(msc)` | Prepare RAM storage and connect it to MSC before USB startup. |
| `UsbSerial.read()` | Accumulates bytes until a newline before interpreting a command. |
| `keyboard.write()` | Sends text following `type ` as keyboard input, to a different destination from the CDC reply. |

CDC is a stream: one host write need not correspond to one read. `lineBuffer` defines command boundaries independently of USB transfers. For an application, reject overlong lines rather than silently executing truncated commands.

Changing a disk from the sketch while the host has it mounted can conflict with host caches. See [MSCFatRamDisk](../MSCFatRamDisk/README.md) for reading files after an eject notification.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[CompositeHidCdcMsc.ino](CompositeHidCdcMsc.ino)
