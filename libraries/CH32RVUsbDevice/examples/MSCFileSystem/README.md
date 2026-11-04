# MSCFileSystem

日本語: [README.ja.md](README.ja.md)

One FAT volume for the sketch and a PC. The sketch writes `FROM_CH32.TXT`,
then shows the volume to the PC as a USB drive; when the PC ejects it, the
sketch takes it back, lists what is there (the PC's files too) and shows it
again.

## Hardware and connections

A board with USB device and 48 KB of RAM, its USB port on a PC, and a UART
monitor on `Serial` (`RX` / `TX`).

## Steps and expected results

1. Reset. The UART shows `FILES` (empty the first time) and `SHOWN_TO_PC`.
2. Open the drive on the PC: `FROM_CH32.TXT` has one line. Copy a file onto it.
3. Eject the drive on the PC. The UART shows `EJECTED`, `FILES` with the
   PC's file and `FROM_CH32.TXT`, and `SHOWN_TO_PC`; the drive comes back with
   one more line in `FROM_CH32.TXT`.

## Settings and limits

Only one side may have the volume at a time: both keep FAT sectors in a cache
and neither sees the other's writes. The sketch mounts it only while the PC
does not have it (`mediaPresent(false)`), and shows it only unmounted. A PC
that never ejects keeps it. The 32 KB volume is FAT12 and is lost on reset.

## API usage and adaptation

| API | Use in this example |
|---|---|
| `CH32RVUsbDeviceMscBlockStorage shared(disk)` | Serves the MSC function from the same block device the FatFS mounts |
| `shared.attach(msc)` | Block count, block size, writable from the device |
| `shared.onEject(...)` | The PC ejected it: through `deliverEvents()` |
| `msc.mediaPresent(true / false)` | Hand the medium to the PC, take it back |
| `fat.begin()` / `fat.end()` | Mount only while the PC does not have it |
