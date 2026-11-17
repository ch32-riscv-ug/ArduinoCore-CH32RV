# SerialUpdate

日本語: [README.ja.md](README.ja.md)

Receives a length and an image over the UART (`Serial`), checks it, then switches to it.

## Hardware and connections

A board and a USB-serial adapter on `Serial`'s UART; Python with pyserial on the PC.

## Steps and expected results

1. After the upload it prints `Update: up to <bytes> bytes`: the largest image it takes.
2. Send the `.bin` of the sketch to switch to with the Python line in the sketch's opening comment (a length line,
   then the bytes).
3. After `OK, restarting into it` the new sketch starts.
4. A transfer that stops prints `Stream Read Timeout`, a file that is no sketch `Wrong Magic Byte`; this sketch stays.

## Settings and limits

- The image can be as large as "flash minus this sketch": on V003 (16 KB), sketches up to about 10 KB.
- Losing power during the switch (the copy over the sketch) leaves a part-written sketch; a probe repairs it.

## API usage and adaptation

| API | Use in this example |
|---|---|
| `Update.begin(size)` | Declares the size; `Not Enough Space` when staging cannot take it. |
| `Update.writeStream(Serial)` | Reads until `size` bytes came; a silence of `Serial.setTimeout()` fails it. |
| `Update.end()` | Checks the image and makes it the one the restart installs. |
| `CH32RV.restart()` | Switches and starts it. |

## Related

- [The Update library](../../README.md)

[SerialUpdate.ino](SerialUpdate.ino)
