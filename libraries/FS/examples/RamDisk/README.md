# RamDisk

日本語: [README.ja.md](README.ja.md)

A FAT volume in 32 KB of RAM: format it, write a file, append to a log in a
directory made on the way, list everything and read the log back.

## Hardware and connections

A board with 48 KB of RAM or more and the serial monitor (`Serial`).

## Steps and expected results

Open the monitor at 115200 baud and reset. It prints the FAT type, the size
and the free space, the tree (`config.json`, `logs/` with `today.txt`), the
three log records and the space used.

## Settings and limits

64 blocks (32 KB) is the smallest volume FatFs makes; the volume is FAT12 and
is lost on reset. On the small series (X035 class) `format()` is not built in,
so `begin(disk, true)` cannot make the volume there.

## API usage and adaptation

| API | Use in this example |
|---|---|
| `fs::RamBlockDevice(storage, blocks)` | The blocks in a RAM array. Any `fs::BlockDevice` works the same |
| `fat.begin(disk, true)` | Mount, formatting the medium when it holds no volume |
| `fat.open(path, FILE_APPEND, true)` | `true` makes the missing `/logs` |
| `dir.openNextFile()` | Lists a directory; `e.path()` opens a subdirectory |
| `fat.end()` | Unmount; what was written is flushed |
