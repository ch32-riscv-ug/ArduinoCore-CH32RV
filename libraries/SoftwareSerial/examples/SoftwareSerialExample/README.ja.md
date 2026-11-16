# SoftwareSerialExample

[English](README.md)

ハードウェアの `Serial` と、普通の 2 本の端子の SoftwareSerial を中継します。

## 必要なものと配線

端末用に `Serial` へ USB-UART アダプタ（アダプタ RX←ボード TX、TX→RX、GND 共通）。SoftwareSerial 側には 9600 8N1 の
相手（もう 1 つのアダプタなど）をつなぎます。相手の RX をスケッチの `SOFT_TX`（A2）、相手の TX を `SOFT_RX`（A1）へ、
GND を共通にします。I/O 電圧を合わせてください。

## 操作と期待する結果

`Serial` の端末を 115200 で開きます。打った文字が `SOFT_TX` から出て、相手が `SOFT_RX` に送ったものが端末に出ます。
SoftwareSerial の 2 本をつないで折り返しても何も戻りません。送信と受信は重ならない（半二重）ためです。

## API と処理の流れ

`soft.begin(9600)` で始め、そのオブジェクトが受信する側（listen）になります。`write()` は送り終えてから戻るので、
中継は自然に送る速さに合います。`overflow()` は 64 バイトのバッファが満杯で捨てたバイトがあったことを知らせます。

## 改造するとき

`SOFT_RX` / `SOFT_TX` はどの端子でもかまいません。速さや形式も変えられます（`begin(9600, SERIAL_8E1)`）。115200 を
超えると、遅い石では受信がビットを取りこぼすことがあります。ハードウェアの `Serial` が端子に届くなら、そちらを使います。

## 関連する文書

- [スケッチ](SoftwareSerialExample.ino)
- [SoftwareSerial](../../README.ja.md)
