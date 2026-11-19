# UsbConsole

[日本語](README.ja.md)

A tool for when a host application does not react as expected (the role of EspUsbDevice's EspUsbDeviceConsole). From
a terminal on the hardware UART it sends HID (keyboard, mouse, raw reports) and vendor transfers one at a time, and logs
everything the host sends down (HID output and protocol, vendor control requests, bulk OUT).

## What you need and the wiring

- A part with a USB device port, and a USB cable to the host under test. On a V203 it takes 98 % of 32 KB (a larger V203
  part or another series is better).
- A terminal at 115200 bps, 8N1, on `RX` / `TX` (the commands are typed there too).

## What to do and what to expect

| Command | What it does |
|---|---|
| `help` / `state` | The list / enumerated, suspended, LEDs, HID protocol, vendor state |
| `text <string>` | Sent as keystrokes (into the host's focused window) |
| `key <usage> [modifiers]` / `hold ...` / `release` | Tap a key / hold it / release (usage is HID's number, 0x04 is a) |
| `mouse <dx> <dy> [buttons] [wheel]` / `click <1\|2\|4>` | Mouse movement / a click |
| `report <id> <hex...>` | A raw report on HID instance 0 (without the ID) |
| `vendor <hex...>` / `vendortext <string>` | Sent on the vendor bulk IN |

What the host sends shows on lines starting with `HOST_`.

```
MOUNTED
HOST_HID_PROTOCOL instance=0 protocol=report
key 0x04
SEND key usage=0x04 modifiers=0x00 ok=1
HOST_HID_OUTPUT leds=0x02 num=0 caps=1 scroll=0
HOST_VENDOR_CONTROL stage=1 type=0xc0 request=0x01 value=0x0000 index=0x0000 length=17 actual=0
HOST_VENDOR_OUT 5 68 65 6c 6c 6f
```

## The API and the flow

- Sending: `keyboard.tapUsage()` / `pressUsage()` / `releaseAll()` / `write()`, `mouse.move()` / `click()`,
  `device.sendHidReport()`, `vendor.write()`.
- Receiving: `keyboard.onOutputReport()` / `onProtocol()`, `vendor.onRxData()` (bulk OUT handed over as it is),
  `vendor.onControlRequest()` (every request is answered, so the host sees success instead of a stall; change
  `handleVendorControl()` to replay the protocol you are studying).
- The callbacks run from `deliverEvents()` in `loop()`.
