# MSC

日本語: [README.ja.md](README.ja.md)

An eight-KiB RAM-backed raw block device for SCSI and block-transfer checks.

## Hardware and connections

A supported device board, PC and UART monitor. The disk alone uses eight KiB of RAM.

## Steps and expected results

1. Check UART for `blocks=16 block_size=512 bytes=8192`.
2. Locate the small USB mass-storage device on the PC.
3. A block viewer can inspect the `55 aa` signature at the end of sector zero.

## Settings and limits

There is no FAT filesystem, so failure to mount in a file browser is expected. Use [MSCFatRamDisk](../MSCFatRamDisk/README.md) for files. Reset or power loss clears the contents. Formatting is unnecessary for this raw-block example.

## API usage and adaptation

| API | Use in this example |
|---|---|
| `CH32RVUsbDeviceMscRamDisk disk(storage, blocks, size)` | Uses a sketch-owned RAM array with block count and bytes per block. |
| `disk.attach(msc)` | Connects capacity and storage to MSC. Do not proceed to USB startup on failure. |
| `msc.mediaPresent(true)` / `msc.isWritable(true)` | Advertise medium presence and write permission. |
| `msc.onStartStop(...)` | Answers START STOP UNIT; return whether to accept the request. |

Keep `storage` alive while exposed over USB. Its array must cover `blockCount × blockSize`, with RAM left for other work. Reset loses the contents.

For another medium, use `onRead()` / `onWrite()` or a storage object. The byte position is `lba × blockSize + offset`; return the number of requested bytes processed. These and `onStartStop()` run while USB waits for an answer. Unlike notification callbacks, they must not contain long waits or `delay()`.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[MSC.ino](MSC.ino)
