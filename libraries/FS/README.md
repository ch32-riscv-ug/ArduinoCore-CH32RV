# FS — files and directories on FAT (FatFs, with ESP32's FS / File API)

[日本語](README.ja.md)

`FatFS` mounts a FAT12/16/32 or exFAT volume on a block device and gives
ESP32's `FS` / `File` API on it: `open()`, `exists()`, `remove()`,
`rename()`, `mkdir()`, `rmdir()`, and `File`'s `Stream`, `seek()`,
`openNextFile()`. The file system is FatFs R0.16 with its author's patches 1
and 2 (CVE-2026-6682, -6683 and -6687).

```cpp
#include <FatFS.h>

static uint8_t storage[64 * 512];
fs::RamBlockDevice disk(storage, 64);
FatFS fat;

void setup() {
    fat.begin(disk, true);                      // format it if it holds no volume
    File f = fat.open("/logs/today.txt", FILE_APPEND, true);   // true: make /logs
    f.printf("%lu\n", millis());
    f.close();
}
void loop() { }
```

## What a volume sits on

`fs::BlockDevice` (FSBlockDevice.h): numbered 512-byte blocks.

| Medium | Block device |
|---|---|
| RAM | `fs::RamBlockDevice` (here). At least 64 blocks (32 KB) |
| USB mass storage shown to a PC | the same block device through `CH32RVUsbDeviceMscBlockStorage` (CH32RVUsbDevice). Only one side has the volume at a time: [MSCFileSystem](../CH32RVUsbDevice/examples/MSCFileSystem/README.md) |
| SD card (SPI, SDIO), eMMC, a USB stick through the USB host | not yet (docs/filesystem.ja.md) |

A sketch can implement `fs::BlockDevice` for anything else: `readBlocks()`,
`writeBlocks()`, `blockCount()`, and optionally `begin()`, `sync()`,
`writable()`, `eraseBlocks()`.

An external SPI NOR flash is not a file system medium here: use it as a raw
image, and put files on an SD card (docs/filesystem.ja.md).

## API

`FatFS`:

| | |
|---|---|
| `begin(device, formatIfEmpty = false)` | Mount. `formatIfEmpty` formats a medium with no FAT volume (where `format()` is built in) |
| `end()` | Unmount; open files are closed and flushed, and every `File` of them is dead |
| `format()` / `format(device)` | A new empty volume: FAT12/16/32 by size, exFAT from about 4 GB; an MBR partition from 32 MB, a single volume below |
| `mounted()`, `fatType()`, `totalBytes()`, `usedBytes()`, `freeBytes()` | |

`FS` and `File` are ESP32's. Paths are absolute (`/dir/file.txt`). Modes are
`"r"`, `"r+"`, `"w"`, `"w+"`, `"a"`, `"a+"`; `"r"` also opens a directory.
`open(path, mode, true)` makes the missing parent directories of a file opened
for writing.

Where it differs from ESP32, and why:

- **`"w"` cuts the old file at `flush()` / `close()`, not at `open()`.** It
  writes over the old content, so a small file rewritten in place (a
  configuration under 512 bytes) is the old one or the new one after a power
  cut, never empty. While the cut is pending, `size()` and `read()` see the
  new file only.
- **`seek()` past the end fails** (FatFs would grow a file opened for writing).
- **`rename()` fails when the target exists**, and `mkdir()` when the directory
  does (FatFs). There is no replacing rename on FAT.
- `File` handles: copies share one open file and the last copy closes it;
  `close()` ends it for every copy, and a copy used afterwards does nothing.
- `getLastWrite()` is FAT's local time (2-second steps) through `mktime()`;
  without timestamps (the small series) it is 0. New files take `time()` as
  local time, or 2026-01-01 while the clock was never set.

## Build-time sizes

Set any of these in the sketch's `build_opt.h` (`-DFF_FS_EXFAT=0`).

| | Default | Notes |
|---|---|---|
| `FS_MAX_OPEN_FILES` | 4 (V003: 2) | Files and directories open at once, over all volumes. Listing with `e = dir.openNextFile()` holds three |
| `FS_PATH_MAX` | 128 (X035 class: 64, V003: 40) | The longest path a `File` keeps; a longer one does not open |
| `FF_VOLUMES` | 2 (small series: 1) | Volumes mounted at once |
| `FF_FS_EXFAT` (and GPT) | on (small series: off) | +6.5 KB |
| `FF_FS_NORTC` | off (small series: on) | Timestamps from `time()`: +5.7 KB (`mktime()` / `localtime()`) |
| `FF_USE_MKFS` | on (small series: off) | `format()` |
| `FF_FS_TINY` | off (small series: on) | Files share the volume's sector buffer: 50 B a file instead of 600 |
| `FF_USE_LFN` | on (V003: off) | Long names (UTF-8). Off, names are 8.3 |
| `FF_FS_READONLY` | off | Reading only |

The small series are V002-V007, M007, X033, X035 and M030.

Flash and RAM over an empty sketch (mount, append, read back; from
docs/filesystem.ja.md):

| | Flash | RAM |
|---|---|---|
| CH32V307, defaults | +27.1 KB | +5.7 KB |
| CH32V307, no exFAT | +20.6 KB | +4.7 KB |
| CH32V307, no exFAT, timestamps, format, lock; one volume | +14.3 KB | +4.4 KB |
| CH32X035, defaults | +14.0 KB | +2.1 KB |
| CH32X035, reading only | +9.0 KB | +2.1 KB |
| CH32V003, defaults (8.3 names) | +10.3 KB | +1.3 KB |
| CH32V003, reading only | +6.1 KB | +1.3 KB |

RAM includes the sketch's 512-byte test buffer.

## Checked on

The host test (tests/unit/fs) builds the library for the PC with the address
and undefined-behaviour sanitizers: the API's behaviour, volumes Linux's
`mkfs.fat` / `mkfs.exfat` makes (FAT12/16/32, exFAT; with and without an MBR)
written and found clean by `fsck` with the names Linux reads, volumes the
library formats, two volumes at once; in the large and the small series'
configuration.

On a chip (tests/peer/usb, case usb-peer-fs, CH32X035F8U6 with an ESP32-S3):
a volume FatFS formats and writes, shown over USB mass storage, is mounted by
the S3 with ESP-IDF's FAT, which reads it and writes a file; the DUT takes it
back, finds its own files (a long-named directory and file among them) and
reads the S3's; then it rewrites a file in place and the S3 reads the new
content. Twice, passing (2026-11-04).
