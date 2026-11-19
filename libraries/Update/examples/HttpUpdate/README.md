# HttpUpdate

[日本語](README.ja.md)

An Ethernet board fetches a new sketch from an HTTP server on the PC, checks it and installs it (the role of
arduino-esp32's HTTPUpdate). Once at start it GETs `UPDATE_PATH` from `UPDATE_HOST`, the length taken from
Content-Length.

## What you need and the wiring

- A board with an Ethernet MAC (V208, V307 / V317, V407 / V467, H415 to H417), its RJ45 on a LAN with DHCP, and a PC on
  the same LAN.
- The serial monitor is `Serial`, 115200 baud.

## What to do and what to expect

1. In a folder holding the new sketch's `.bin` (Arduino's "Export compiled binary") named new.bin, run
   `python3 -m http.server 8000`.
2. Set the sketch's `UPDATE_HOST` to the PC's IP and upload it.

```text
Address 192.168.1.81
image 3420 bytes, room for 210944
3420 bytes in 247 ms
OK, restarting into it
```

and the board starts the new sketch. A file that is not a sketch gives `Wrong Magic Byte`, one too big
`Not Enough Space`, and this sketch stays. Both checked on a CH32V307 (2026-11-20).

## The API and the flow

| API | Its use here |
|---|---|
| `NetworkClient` | A GET in HTTP/1.0; the header read up to the empty line for Content-Length. |
| `Update.begin(length)` / `Update.capacity()` | The size declared; the largest is the flash less this sketch. |
| `Update.writeStream(client)` | The body streamed in; a stall ends it after `setTimeout()`. |
| `Update.end()` / `CH32RV.restart()` | Checked, then installed and started. |

Nothing is encrypted or authenticated. Where others can reach the server, consider HTTPS (NetworkClientSecure) and a
signature on the image. Power lost during the swap (the copy to the start) leaves the sketch half written (a probe
recovers it).
