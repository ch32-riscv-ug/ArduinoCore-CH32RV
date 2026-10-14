# CH32RVTinyUSB

The [TinyUSB](https://github.com/hathach/tinyusb) stack that
[CH32RVUsbDevice](../CH32RVUsbDevice/) is built on. Sketches do not include it
directly.

- `src/` is a pinned upstream snapshot; `vendor/tinyusb.lock.toml` records the
  commit and a hash per file, and `tools/vendor/vendor_tinyusb.py --check`
  verifies it.
- One local patch: `CFG_TUD_APP_DRIVERS_ONLY` empties TinyUSB's built-in class
  table, so only the classes a sketch declares are linked. It is listed with
  its reason in `tools/vendor/vendor_tinyusb.py`.
- Ours, not upstream: `src/tusb_config.h` and `src/ch32x035.h`.

Sizes (how many CDC ports, buffer lengths) can be changed per sketch from
`build_opt.h`; see `src/tusb_config.h` and [docs/usb.ja.md](../../docs/usb.ja.md).
