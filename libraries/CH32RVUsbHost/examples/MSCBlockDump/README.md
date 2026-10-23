# MSCBlockDump

日本語: [README.ja.md](README.ja.md)

USB storage INQUIRY/capacity and a hex dump of the first block, without a filesystem.

## Hardware and connections

A USB drive or card reader with media inserted, powered host wiring and a 115200-baud UART monitor.

## Steps and expected results

1. Once ready, UART prints vendor/product, block count and block size.
2. For 512-byte blocks, LBA 0 is read and dumped in 16-byte lines.
3. Unplug/replug to perform the one-shot dump again.

## Settings and limits

This sketch only reads. Non-512-byte media prints capacity but skips the fixed-buffer read. LBA 0 may be a partition table or filesystem boot sector. Insert media before testing: an empty reader may not reach readiness.

## API usage and adaptation

| API | Use in this example |
|---|---|
| `mscReady()` / `mscWaitReady()` | Check the MSC function and medium readiness; the latter can wait. |
| `mscInquiry(inquiry)` | Reads vendor/product information. |
| `mscCapacity(blocks, size)` | Returns block count and bytes per block; their product is total capacity. |
| `mscReadBlocks(lba, buffer, count)` | Reads count blocks, not count bytes, starting at LBA. |

Allocate `count × blockSize` bytes and use sufficiently wide arithmetic for capacity. Matching the reported block size to the array prevents an oversized read.

The first block is not the first byte of a file. File access requires partition/filesystem processing. For retries, also change the sample's `dumped` flag, which is set before attempting the read, and bound retries by count or time.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbHost API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[MSCBlockDump.ino](MSCBlockDump.ino)
