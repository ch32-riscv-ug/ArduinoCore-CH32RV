# TinyUSB

A pinned copy of [TinyUSB](https://github.com/hathach/tinyusb) 0.21.0 (MIT).
**Not wired up yet** - there is no `library.properties`, so arduino-cli does
not see it as a library and nothing compiles it. It is here because
[ADR-0012](../../docs/adr/0012-usb-stack.ja.md) chose TinyUSB as this core's
USB stack, and chose to keep it inside the repository rather than as an
external dependency, since patches are expected.

## What is here

Only part of upstream `src/`: everything outside `portable/`, plus
`portable/wch` (the CH32 device FS/HS and host FS drivers) and
`portable/st/stm32_fsdev` (CH32V20x port0 is an ST FSDEV clone and TinyUSB uses
that driver for it). Thirty other vendors' drivers would compile into nothing.

`vendor/tinyusb.lock.toml` records the tag, the licence and the SHA-256 of
every file, plus a `patches` list. `tools/vendor/vendor_tinyusb.py --check`
verifies the copy offline and is run by CI, so an edit that nobody wrote down
fails the build instead of being reverted silently at the next version bump.

## What is left to do

- The board glue: clock, interrupt and `tusb_config.h`
- The vendor-header shim the drivers expect
  ([R-23](../../docs/research/tinyusb-vendor-header.ja.md))
- PLL support, because USB needs 48 MHz and only CH32X035 has that from HSI
  ([R-22](../../docs/research/usb-stack.ja.md))
