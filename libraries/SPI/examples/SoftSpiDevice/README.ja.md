# SoftSpiDevice

[English](README.md)

ハードウェアの SPI の端子が使えないときに、どの 4 本の GPIO でも SPI の機器（ここでは SPI Flash の JEDEC ID）を使います。`SoftSPI` は `SPI` と同じ API をソフトで実装したものです。

## 配線と操作

SPI Flash の SCK・MISO（DO）・MOSI（DI）・CS をスケッチの 4 本に、電源と GND。WP と HOLD は VCC に。1 秒ごとに `JEDEC ID: EF 40 18` のように出ます（W25Q128 の例）。何もつながないと `FF FF FF` か `00 00 00`。

## API と改造時の注意

`SoftSPI bus(sck, miso, mosi)` のあとは `SPI` と同じで、`SPISettings` のモードとビット順が効きます。速さはソフトなので系列のクロックしだいで、指定より遅くなることがあります。CS は自分で操作します。

## 実行前の準備

ボードと正確な Part Number を選び、端子を回路図と照合します。生成された `sketch.yaml` にビルドの profile があります。ビルドの成功は、配線や実機の動作の確認とは別です。
