# Update

日本語: [README.ja.md](README.ja.md)

Replaces the running sketch with a new image it received. The names and error codes are arduino-esp32's `Update`.
The sketch picks the transport (UART, a USB CDC or vendor interface, the network). CH32RVUsbDevice's
`CH32RVUsbDeviceDfu` and `CH32RVUsbDeviceMscFirmwareDisk` take USB DFU downloads and UF2 files the same way.

```cpp
#include <CH32RV.h>
#include <Update.h>

if (!Update.begin(size)) { Update.printError(Serial); }
Update.write(buffer, n);          // in order, as often as needed
if (Update.end()) {
    CH32RV.restart();             // the switch happens here (Update.restart() does the same)
}
```

## How it works

- The image is written to the flash after the sketch (staging). `Update.capacity()` is the largest it takes: about
  "flash minus this sketch", so a sketch that receives updates should stay under half the flash. `CH32RV_FLASH_STORE`
  areas are kept.
- Before `end()` returns true it checks: every page reads back, the image starts with this core's reset entry, the
  length, and the staged flash's CRC-32 equals that of the bytes written. Otherwise false, and nothing changed.
- The switch happens at the restart (`CH32RV.restart()` / `Update.restart()`): a copy over the sketch, from RAM, with
  interrupts off. Any other restart (power, a watchdog, NRST) keeps the running sketch.
- Losing power during the copy (about a second for 64 KB) leaves a part-written sketch; the factory bootloader and the
  debug interface still reach the chip.
- A sketch running a USB device ends it first (`USBDevice.end()`): it does not answer during the copy.

Details: [the USB guide's firmware update section](../../docs/usb.ja.md#ファームウェア更新dfu-のダウンロードuf2-のドライブ)
(Japanese) and the core's `ch32rv_update.h`.

## Differences from arduino-esp32

| arduino-esp32 | Here |
|---|---|
| `begin(size, command, ledPin, ledOn, label)` | The same. `command` is `U_FLASH` only (anything else is `UPDATE_ERROR_BAD_ARGUMENT`); `label` is ignored (no partitions) |
| `write()`, `writeStream()`, `end(evenIfRemaining)`, `abort()`, `printError()`, `errorString()`, `getError()`, `hasError()`, `isRunning()`, `isFinished()`, `size()`, `progress()`, `remaining()`, `onProgress()` | The same |
| `setMD5()`, `md5String()` | Not here (the staged CRC-32 is checked instead: `crc32()`) |
| `canRollBack()`, `rollBack()` | false: no previous image is kept |
| — | `capacity()`, `beginRandomAccess()` / `writeAt()` (any order, for UF2), `isPending()` / `cancelPending()`, `restart()`, `UPDATE_ERROR_VERIFY` (20) |

## Examples

- [SerialUpdate](examples/SerialUpdate/README.md): receives a length and an image over the UART and switches to it.
