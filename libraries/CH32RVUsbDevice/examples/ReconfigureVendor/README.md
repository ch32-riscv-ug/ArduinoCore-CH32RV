# ReconfigureVendor

[日本語](README.ja.md)

Stops the USB device while it runs (`device.end()`) and starts it again with other buffer sizes. Send `r` on the UART
to switch the vendor function's rings between 128 and 2048 bytes. Meanwhile the vendor bulk data is echoed, nothing
dropped (as EspUsbDevice's ReconfigureVendor).

## What you need and the wiring

A part with a USB device port, and a USB cable to the PC. A UART monitor at 115200 bps (`RX` / `TX`; `r` is sent
there). A tool on the PC that sends and receives vendor bulk data (the PyUSB steps of [USBVendor](../USBVendor/README.md)
and the like).

## What to do and what to expect

At start it prints `Started: RX=127 TX=127`. Sending `r` makes the device leave and come back on the PC (a new
enumeration) and prints `Started: RX=2047 TX=2047`; `r` again goes back to 128. Vendor data from the PC comes back
unchanged at either size.

## The API and the flow

| API | Its use here |
|---|---|
| `device.end()` | Stops the device: a disconnect for the host; what was in the rings is dropped. |
| `vendor.configure(buffers)` | The ring sizes; only while stopped. |
| `device.begin()` | Starts again with the new settings. |
| `vendor.readCapacity()` / `writeCapacity()` | The actual ring sizes (one byte marks the free space). |
