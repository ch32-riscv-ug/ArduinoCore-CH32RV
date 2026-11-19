# SpiSharedBus

[English](README.md)

1 本の SPI（SCK・MISO・MOSI）に、速さとモードの違う 2 つの機器（SPI Flash とセンサ）をつなぎ、それぞれの CS で選んで交互に読みます。

## 配線と操作

SCK・MISO・MOSI を両方の機器に、CS はスケッチの `FLASH_CS`・`SENSOR_CS` にそれぞれ。電源と GND も。1 秒ごとに `flash EF 40 18, sensor 0x6A` のように出ます（値は機器しだい）。

## API と改造時の注意

機器ごとに `SPISettings`（速さ・ビット順・モード）を持ち、`beginTransaction()` で切り替えてから CS を Low にします。終わったら CS を High にして `endTransaction()`。`begin()` の前に両方の CS を High にしておくと、起動の途中で機器が誤って選ばれません。割り込みから同じバスを使うときは、`SPI.usingInterrupt()` で守ります。

## 実行前の準備

ボードと正確な Part Number を選び、端子を回路図と照合します。生成された `sketch.yaml` にビルドの profile があります。ビルドの成功は、配線や実機の動作の確認とは別です。
