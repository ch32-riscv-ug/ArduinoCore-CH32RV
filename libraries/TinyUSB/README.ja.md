# TinyUSB

[English](README.md)

[TinyUSB](https://github.com/hathach/tinyusb) の固定 source copy です。
Arduino library として認識させる `library.properties` はなく、core の USB API にも結線されていません。
したがって、この directory があるだけで sketch に TinyUSB は link されません。

含める範囲は TinyUSB の共通 source、CH32 driver、CH32V20x が使う STM32 FSDEV driver です。
他 vendor の portable driver は含めません。

upstream tag、license、全 file の SHA-256、許可した patch は `vendor/tinyusb.lock.toml` が正本です。

```sh
uv run --no-project python tools/vendor/vendor_tinyusb.py --check
```

USB stack として TinyUSB を選ぶ理由は
[ADR-0012](../../docs/adr/0012-usb-stack.ja.md) を参照してください。
