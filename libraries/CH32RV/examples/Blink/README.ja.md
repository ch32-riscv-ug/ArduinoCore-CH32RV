# Blink

[English](README.md)

GPIO を High/Low に切り替え、スケッチが実行されることを LED で確認します。

## 必要なものと配線

基板の LED、または GPIO と GND の間に電流制限抵抗を直列にした LED を接続します。使用する端子を `LED_BUILTIN` として指定します。

## 操作と期待する結果

書き込むと LED が一定間隔で点滅します。Low で点灯する基板では点灯と消灯の順が逆になります。

## API と処理の流れ

`pinMode(..., OUTPUT)` で出力へ切り替え、`digitalWrite()` で論理値を指定します。`delay()` の引数は ms です。

## 改造するときの注意

Generic board は基板の LED 位置を決めません。コンパイル時に `LED_BUILTIN` が無いと言われたら、実際の LED 端子を指定します。複数の入力を扱う段階では [Timing](../Timing/README.ja.md) の時刻比較へ進みます。

### Generic boardでLED端子を指定する

例えば回路図上のLEDがPC13なら、リポジトリのルートから次のように指定してビルドします。

```sh
arduino-cli compile --fqbn ch32-riscv-ug:ch32rv:CH32V203 \
  --build-property build.extra_flags=-DLED_BUILTIN=PC13 \
  libraries/CH32RV/examples/Blink
```

ボード指定とPC13は実際の対象へ置き換えます。スケッチ内で定義する場合も、`#ifndef LED_BUILTIN`の確認より前に置きます。

## 関連資料

- [スケッチ](Blink.ino)
- [基本サンプルの準備と一覧](../README.ja.md)
- [スケッチの組み立て方](../../../../docs/sketch-guide.ja.md)
