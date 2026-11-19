# VendorBulkPeer

[日本語](README.ja.md)

A vendor bulk echo that never loses a byte, run against [vendor_bulk_peer.py](vendor_bulk_peer.py) (PyUSB) on the PC.
The device reads only as much as its bulk IN side can take right now, so when the PC stops reading, the device stops
reading too and the PC's writes wait (back-pressure instead of dropped data). It reproduces partial transfers and a
stalled sender.

## What you need and the wiring

- A part with a USB device port, and a USB cable to the PC. A UART monitor at 115200 bps on `RX` / `TX`.
- On the PC: Python, PyUSB (`pip install pyusb`) and libusb. On Linux, access to the device; on Windows, WinUSB bound to
  it (as for [USBVendor](../USBVendor/README.md)).

## What to do and what to expect

After the upload, with USB connected, run `python3 vendor_bulk_peer.py` on the PC.

```
steady: 30104 bytes sent, 30104 back, same
stalled: the write timed out (...)
stalled: the write took 704 bytes before the device stopped reading, 704 came back
after resuming: 8192 of 8192 back, in order, none lost
```

1. **steady**: chunks of 1 to 300 bytes written while reading. Everything comes back, in order.
2. **stalled**: 8 KB written without reading. Once the device's IN side is full it stops reading, and the PC's write
   times out part-way (how far depends on the device's FIFOs and endpoint sizes). Then the PC reads and sends the
   rest, and everything comes back in order.

The UART prints `moved ... bytes, IN full ... times` every second; `IN full` grows during the stalled phase.

## The API and the flow

| API | Its use here |
|---|---|
| `vendor.writeAvailable()` | Room in the IN FIFO. No more than this is read, so `write()` always takes all of it. |
| `vendor.available()` / `read()` | What arrived on OUT. What is not read stays in the endpoint, and the host's writes wait. |
| `vendor.write()` / `flush()` | Send. `write()` takes what fits and returns how much (it does not wait for all of it). |

[USBVendor](../USBVendor/README.md)'s echo does not look at `write()`'s return value, so what does not fit while the
PC is not reading is dropped. This one bounds the reads by the room and loses nothing.
