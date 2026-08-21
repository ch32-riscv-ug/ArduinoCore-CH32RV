# TinyUSB

[TinyUSB](https://github.com/hathach/tinyusb) 0.21.0(MIT)の固定コピーです。
**まだ結線していません** — `library.properties`を置いていないので
arduino-cliはライブラリとして認識せず、何もコンパイルされません。
ここにあるのは[ADR-0012](../../docs/adr/0012-usb-stack.ja.md)で
TinyUSBを採用し、かつ**パッチを当てる可能性があるため内部に持つ**と決めたためです。

## 何が入っているか

上流`src/`の一部だけです。`portable/`以外の全部に加えて、
`portable/wch`(CH32のdevice FS/HSとhost FSドライバ)と
`portable/st/stm32_fsdev`(CH32V20xのport0はSTのIPクローンで、
TinyUSBはそのドライバを使います)。
他ベンダのドライバ30種を持ってきても、コンパイルすると何も残りません。

`vendor/tinyusb.lock.toml`にtag・ライセンス・**全ファイルのSHA-256**と
`patches`欄を記録しています。`tools/vendor/vendor_tinyusb.py --check`が
オフラインで照合し、CIが回します。
記録されていない改変はビルドを落とすので、
**次のバージョン更新で黙って消える**ことがありません。

## 残っていること

- board側の結線(クロック・割込み・`tusb_config.h`)
- ドライバが要求するベンダヘッダのshim
  ([R-23](../../docs/research/tinyusb-vendor-header.ja.md))
- PLL対応。USBには48MHzが要り、HSIでそれが出るのはCH32X035だけです
  ([R-22](../../docs/research/usb-stack.ja.md))
