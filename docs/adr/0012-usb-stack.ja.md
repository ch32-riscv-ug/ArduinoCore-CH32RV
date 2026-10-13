# ADR-0012: USB stack には TinyUSB を使う

- Status: Accepted

## 決定

USB device / host を実装する際は TinyUSB を使用し、独自 stack や WCH EVT の USB API を platform API にしません。
必要な CH32 対応は可能な限り TinyUSB upstream へ反映し、恒久 fork を持ちません。

固定した TinyUSB source は `libraries/TinyUSB/src/`、revision と hash は `vendor/tinyusb.lock.toml` で管理します。
Arduino からは `libraries/TinyUSB`（`TinyUSB.h`、`SerialUSB`）として使います。device の CDC を CH32X033 / X035 で結線済みで、
他系列と host は未実装です。stack の処理は core の weak hook `ch32rv_usb_task()` から、`loop()` の後と `delay()` の中で行います。

## 理由

TinyUSB は device と host の共通 API、公開 source、CH32 向け driver を持ちます。系列ごとの EVT USB API を直接使うと
Arduino 側の API と保守が分裂し、host 側には source が公開されていない部分もあります。upstream を共有する方が
protocol stack を独自保守せず、他 platform の修正も利用できます。
