# FirmwareMSC

日本語: [README.ja.md](README.ja.md)

A USB drive: copy a `.uf2` (or the `.bin`) onto it and the board switches to that firmware.

## Hardware and connections

A board of a series with a USB device controller and 16 KB of RAM or more, and a PC. Progress goes to `Serial` (UART).
The image can be as large as "flash minus this sketch".

## Steps and expected results

1. Upload this sketch: a drive named `FIRMWARE` appears, holding `README.TXT`.
2. Make a UF2 from the `.bin` of the sketch to switch to, with the family ID `Serial` prints (per series, the table in
   the USB guide): `python3 uf2conv.py -c -b 0x08000000 -f <family> <sketch>.ino.bin -o sketch.uf2`
3. Copy `sketch.uf2` onto the drive: `received` grows, and once every block came the drive goes away about 0.5 s later
   and the new sketch starts.
4. A `.uf2` with another series' family ID is refused on its first block (`update error 30`); this sketch stays.
5. The `.bin` itself works too, as long as the host writes it in order: the switch comes when the file's length is
   reached or the drive is ejected.

## Settings and limits

- `storage` (8 KB) holds the boot sector, the FATs, the root directory and the host's own files; the image goes to flash.
- Put an update path in the image you send too, or the next update needs the factory ISP or a probe.
- How it works, the family IDs and what is checked: [the USB guide's firmware update section](../../../../docs/usb.ja.md#ファームウェア更新dfu-のダウンロードuf2-のドライブ) (Japanese).

## API usage and adaptation

| API | Use in this example |
|---|---|
| `CH32RVUsbDeviceMscFirmwareDisk disk(storage, size)` | The drive. `begin()` lays the volume over the staging flash (false when there is none). |
| `disk.addTextFile()` | A file shown from the start; between `begin()` and `attach()`. |
| `disk.attach(msc)` | Serves `CH32RVUsbDeviceMsc` from it (medium present, writable). |
| `disk.onProgress()` / `disk.onError()` | Bytes written and failures (`CH32RV_UPDATE_*`), from inside the USB service. |
| `CH32RVUsbDeviceMscFirmwareDisk::uf2FamilyId()` | The family ID a `.uf2` needs. |

## Related

- [Common setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and coverage](../../README.md)

[FirmwareMSC.ino](FirmwareMSC.ino)
