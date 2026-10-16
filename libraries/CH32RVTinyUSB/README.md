# CH32RVTinyUSB

[日本語](README.ja.md)

The [TinyUSB](https://github.com/hathach/tinyusb) stack that
[CH32RVUsbDevice](../CH32RVUsbDevice/) and [CH32RVUsbHost](../CH32RVUsbHost/) are built on. Sketches do not include it
directly.

- `src/` is a pinned upstream snapshot; `vendor/tinyusb.lock.toml` records the
  commit and a hash per file, and `tools/vendor/vendor_tinyusb.py --check`
  verifies it.
- `CFG_TUD_APP_DRIVERS_ONLY` / `CFG_TUH_APP_DRIVERS_ONLY` replace the built-in
  device/host class tables with application registrations. Weak-reference
  changes avoid linking an unused stack when both roles are available.
  All patches and reasons are recorded in [the vendor tool](../../tools/vendor/vendor_tinyusb.py)
  and [lock](../../vendor/tinyusb.lock.toml).
- Configuration, series headers, DCD/HCD glue and IRQ files authored by this
  core are listed under `ours` in the lock. `ch32rv_hcd_usbfs.c` is this core's
  host-controller driver, separate from upstream portable drivers.

Sizes (how many CDC ports, buffer lengths) can be changed per sketch from
`build_opt.h`; see `src/tusb_config.h` and [docs/usb.ja.md](../../docs/usb.ja.md).

## Verification and examples

From the repository root:

```sh
python3 tools/vendor/vendor_tinyusb.py --check
```

This offline check verifies the pinned snapshot and recorded patches, not
hardware transfers. Record source changes through the vendor process rather
than editing upstream files and merely updating their hashes. See
[ADR-0012](../../docs/adr/0012-usb-stack.ja.md) and
[vendor policy](../../docs/vendor-policy.ja.md).

Use the [device](../CH32RVUsbDevice/examples/README.md) or
[host](../CH32RVUsbHost/examples/README.md) examples. USB PD is implemented
separately by [CH32RVUsbPd](../CH32RVUsbPd/README.md).
