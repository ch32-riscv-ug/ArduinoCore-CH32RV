# SoftI2CScanner

[English](README.md)

任意の GPIO を使ったソフトウェア I2C で、応答するアドレスを探します。

## 必要なものと配線

`SDA_PIN`・`SCL_PIN` と相手機器、共通 GND、各線の pull-up を接続します。ほかの出力と競合しない GPIO を選び、UART モニタを用意します。

## 操作と期待する結果

スキャンを繰り返し、応答のあるアドレスを表示します。何も見つからず timeout 表示が出る場合は pull-up と線が Low に固定されていないかを確認します。

## API と処理の流れ

`SoftWire bus(SDA, SCL)` を宣言し、`begin()` と `setClock(hz)` で開始します。`endTransmission()` の戻り値で ACK、`getWireTimeoutFlag()` で timeout の発生を区別します。

## 改造するときの注意

タイムアウト flag は確認後に `clearWireTimeoutFlag()` で消します。I2C は open-drain なので、内部の処理だけで外付け pull-up の代わりにはなりません。スキャンを全機器に対して無害な操作とは仮定しません。

## 関連資料

- [スケッチ](SoftI2CScanner.ino)
- [ライブラリの API](../../README.ja.md)
- [サンプル案内](../../../../docs/examples.ja.md)
