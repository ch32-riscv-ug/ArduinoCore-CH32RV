# ADR-0012: USB stack には TinyUSB を使う

- Status: Accepted

## 決定

USB device / host を実装する際は TinyUSB を使用し、独自 stack や WCH EVT の USB API を platform API にしません。
必要な CH32 対応は可能な限り TinyUSB upstream へ反映し、恒久 fork を持ちません。

固定した TinyUSB source は `libraries/CH32RVTinyUSB/src/`、revision と hash は `vendor/tinyusb.lock.toml` で管理します。
Arduino からは `CH32RVUsbDevice`（device）と `CH32RVUsbPd`（PD sink）として使います（[docs/usb.ja.md](../usb.ja.md)）。
device は CH32X033 / X035 で結線済みで、他系列と host は未実装です。stack の処理は core の weak hook
`ch32rv_usb_task()` から、`loop()` の後と `delay()` の中で、controller に event があるときだけ行います。

## 理由

TinyUSB は device と host の共通 API、公開 source、CH32 向け driver を持ちます。系列ごとの EVT USB API を直接使うと
Arduino 側の API と保守が分裂し、host 側には source が公開されていない部分もあります。upstream を共有する方が
protocol stack を独自保守せず、他 platform の修正も利用できます。

## 追記: ライブラリの構成（2026-10-14）

- `CH32RVTinyUSB`（同梱の stack と `tusb_config.h`）、`CH32RVUsbDevice`（device の API）、`CH32RVUsbPd`（PD）に分けます。
  host を作るときは `CH32RVUsbHost` を足し、`CH32RVTinyUSB` を共有します。名前は EspUsbDevice / EspUsbHost に倣い、
  ライブラリ名とクラス名を揃えます。
- device は、本体（`CH32RVUsbDevice`）にクラスのオブジェクトを宣言で付け、`begin(config)` で descriptor を組み立てます。
  TinyUSB の組み込み class driver 表を空にし（`CFG_TUD_APP_DRIVERS_ONLY`、`tools/vendor/vendor_tinyusb.py` に記録した
  唯一の patch）、`usbd_app_driver_get_cb()` で宣言されたクラスの driver だけを渡します。宣言しないクラスはリンクされません
  （X035 で実測: include だけなら 0）。
- build 時に決まるのは、class ごとの個数と buffer の大きさだけです。変える場合は sketch の `build_opt.h` を使います。
- 起動時に USB を自動で始める仕組みは入れません。`setup()` より前に始めても認識時間は変わらず（0.3〜0.5 s）、USB を
  使わない sketch にも費用（X035 で Flash 約 11 KB、RAM 約 2.5 KB）がかかるためです。
- USB からの書き込みの入口は、1200bps touch（CDC）と DFU_DETACH（DFU runtime）の2つです。ch32rv 側は
  `isp flash --entry auto` で両方を扱う予定です。
