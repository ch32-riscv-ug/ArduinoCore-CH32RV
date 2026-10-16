# MSCFatRamDisk

日本語: [README.ja.md](README.ja.md)

A ten-KiB FAT12 RAM disk that reads back a CONFIG.TXT file written by the PC.

## Hardware and connections

A device board with sufficient RAM, a PC file browser and a UART monitor.

## Steps and expected results

1. Find the CH32RV volume and README.TXT.
2. Copy a short CONFIG.TXT into its root.
3. Eject the volume from the PC.
4. Observe UART `MSC_EJECT`, `CONFIG_SIZE`, and `CONFIG_BEGIN`/`CONFIG_END`.

## Settings and limits

Eject flushes PC caches before the sketch inspects files. Only the first 255 bytes are printed. The ten-KiB allocation includes filesystem metadata, so usable file capacity is smaller. Use 8.3 filenames supported by this small FAT implementation. The disk is recreated on reset.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[MSCFatRamDisk.ino](MSCFatRamDisk.ino)
