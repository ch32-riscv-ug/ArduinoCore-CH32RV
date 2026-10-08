# TinyUSB

[日本語](README.ja.md)

This is a pinned source snapshot of [TinyUSB](https://github.com/hathach/tinyusb).
It has no `library.properties` and is not wired into the core USB API, so its presence does not link TinyUSB into sketches.

The snapshot contains TinyUSB's common sources, the CH32 portable drivers, and the STM32 FSDEV driver used by CH32V20x.
Portable drivers for unrelated vendors are excluded.

`vendor/tinyusb.lock.toml` is the source of truth for the upstream tag, license, per-file SHA-256 values,
and allowed patches.

```sh
uv run --no-project python tools/vendor/vendor_tinyusb.py --check
```

See [ADR-0012](../../docs/adr/0012-usb-stack.ja.md) for the stack choice.
