# TinyUSB

[日本語](README.ja.md)

USB device on the chip's own USB port: a CDC serial port (`SerialUSB`) on the vendored
[TinyUSB](https://github.com/hathach/tinyusb) stack ([ADR-0012](../../docs/adr/0012-usb-stack.ja.md)).

## Series

| Series | Status |
|---|---|
| CH32X035 | Checked on hardware (WeAct CH32X035F8U6): enumeration, CDC both ways, 1200 bps touch into the bootloader |
| CH32X033 | Same USBFS block as X035; not checked on hardware |
| Others | Builds, but `SerialUSB` never opens (`operator bool` stays false, writes are dropped) |

## Use

```cpp
#include <TinyUSB.h>

void setup() { SerialUSB.begin(); }
void loop()  { SerialUSB.println("hello"); delay(1000); }
```

The stack runs after every `loop()`, inside `delay()` and in each `SerialUSB` call. A sketch that
blocks elsewhere for a long time makes the port look stalled to the host.

With no host holding the port open, `write()` drops the bytes instead of waiting.

## Into the bootloader (1200 bps touch)

Opening the port at 1200 baud and closing it detaches from USB and resets into the factory USB
bootloader (`1a86:55e0`), which `ch32rv isp flash` writes. With no upload the chip returns to the
application after about 60 s.

```sh
stty -F /dev/ttyACM0 1200          # open and close
ch32rv isp flash sketch.bin --device usb:<bus>-<ports> --wait 5
```

`SerialUSB.setBootTouchEnabled(false)` turns it off; `TinyUSBDevice::enterBootloader()` does it
from the sketch.

If the application can no longer run USB, X033 / X035 still enter the bootloader with PC17 (USB D+)
held high at power-up ([upload-routes.ja.md](../../docs/upload-routes.ja.md)).

## USB IDs

The default is the pid.codes test ID `1209:0001`. A product sets its own:

```sh
--build-property "build.extra_flags=-DCH32RV_USB_VID=0x1234 -DCH32RV_USB_PID=0x5678"
```

`CH32RV_USB_MANUFACTURER` / `CH32RV_USB_PRODUCT` change the strings. The serial number is the
chip's 64-bit unique ID.

## Vendored source

`src/` holds a pinned upstream TinyUSB. `vendor/tinyusb.lock.toml` is the source of truth for the
upstream revision, license, per-file SHA-256 values and allowed patches. CH32X035 USBFS support is
upstream PR #3703, pinned at its merge commit because no release carries it yet.

```sh
uv run --no-project python tools/vendor/vendor_tinyusb.py --check
```

Files this repository adds: `tusb_config.h`, `TinyUSB.h`, `TinyUSB.cpp`, `ch32_tusb_glue.cpp`,
`ch32x035.h`. `ch32x035.h` stands in for the WCH SDK header TinyUSB's register header includes,
and defines only the USBFS register layout, the IRQ number and `NVIC_EnableIRQ`.
