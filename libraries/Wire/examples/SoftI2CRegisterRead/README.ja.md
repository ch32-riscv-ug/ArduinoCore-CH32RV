# SoftI2CRegisterRead

[English](README.md)

ハードウェアの I2C の端子が使えないとき（ほかの機能と重なる、経路が無い）に、どの 2 本の GPIO でも I2C の機器のレジスタを読みます。`SoftWire` は `Wire` と同じ API をソフトで実装したものです。

## 配線と操作

機器の SDA・SCL をスケッチの `SDA_PIN`・`SCL_PIN` に、電源と GND をつなぎ、SDA と SCL に 4.7 kΩ ほどのプルアップ。1 秒ごとに `register 0x75 = 0x68` のように出ます（MPU-6050 の WHO_AM_I の例）。相手がいなければ `no answer, code 2`。

## API と改造時の注意

`SoftWire bus(sda, scl)` のあとは `Wire` と同じです。`endTransmission(false)` でバスを保持し、続く `requestFrom()` が repeated START を出します。クロックは `setClock()` で、ソフトなので 400 kHz より上は系列のクロックしだいです。割り込みが長いと、そのぶんクロックの Low が伸びます（I2C では問題になりません）。

## 実行前の準備

ボードと正確な Part Number を選び、端子を回路図と照合します。生成された `sketch.yaml` にビルドの profile があります。ビルドの成功は、配線や実機の動作の確認とは別です。
