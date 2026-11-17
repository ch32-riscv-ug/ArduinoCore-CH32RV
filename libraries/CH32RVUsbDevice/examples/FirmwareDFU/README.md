# FirmwareDFU

日本語: [README.ja.md](README.ja.md)

Takes a new firmware over USB DFU (download) while the sketch keeps running, checks it, then switches to it.

## Hardware and connections

A board of a series with a USB device controller and a PC (`dfu-util` or `ch32rv`). Progress goes to `Serial` (UART).
The image can be as large as "flash minus this sketch": V203's ANY (32 KB) is too small, so pick the part.

## Steps and expected results

1. Upload this sketch; `1209:0001` appears on the PC.
2. Build the sketch to switch to (this one will do) and send its `.bin`:
   `dfu-util -d 1209:0001 -a Firmware -D <sketch>.ino.bin` (or `ch32rv boot dfu flash <sketch>.ino.bin --format bin`).
3. `Serial` counts `received <bytes>`; at the end the device leaves USB once and comes back as the new sketch.
4. Send a file that is no sketch (a text file, say): dfu-util fails with errWRITE and this sketch keeps running.

## Settings and limits

- The default DFU runtime (the Arduino upload port) stays beside it, so dfu-util needs `-a Firmware`.
- Put `CH32RVUsbDeviceDfu` in the image you send too, or the next update needs the factory ISP or a probe.
- Losing power during the switch (the copy over the sketch) leaves a part-written sketch; the factory bootloader or a
  probe repairs it.
- How it works and what it checks: [the USB guide's firmware update section](../../../../docs/usb.ja.md#ファームウェア更新dfu-のダウンロードuf2-のドライブ) (Japanese).

## API usage and adaptation

| API | Use in this example |
|---|---|
| `CH32RVUsbDeviceDfu dfu` | Adds a DFU-mode interface. It uses no endpoint, so it sits beside any other class. |
| `dfu.onProgress(callback)` | Bytes taken so far. Called inside the USB service: just keep the value. |
| `dfu.onError(callback)` | The DFU status of a failure (`CH32RVUsbDfuStatus`). |
| `dfu.onComplete(callback)` | Not used here; returning `false` refuses a checked image. |
| `dfu.restartWhenComplete(false)` | Leaves the restart to the sketch: `CH32RV.restart()` or `Update.restart()` switches. |

## Related

- [Common setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and coverage](../../README.md)
- [The Update library](../../../Update/README.md)

[FirmwareDFU.ino](FirmwareDFU.ino)
