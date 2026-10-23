# Blink

GPIO を High/Low に切り替え、スケッチが実行されることを LED で確認します。

## 必要なものと配線

基板の LED、または GPIO と GND の間に電流制限抵抗を直列にした LED を接続します。使用する端子を `LED_BUILTIN` として指定します。

## 操作と期待する結果

書き込むと LED が一定間隔で点滅します。Low で点灯する基板では点灯と消灯の順が逆になります。

## API と処理の流れ

`pinMode(..., OUTPUT)` で出力へ切り替え、`digitalWrite()` で論理値を指定します。`delay()` の引数は ms です。

## 改造するときの注意

Generic board は基板の LED 位置を決めません。コンパイル時に `LED_BUILTIN` が無いと言われたら、実際の LED 端子を指定します。複数の入力を扱う段階では [Timing](../Timing/README.ja.md) の時刻比較へ進みます。

## 関連資料

- [スケッチ](Blink.ino)
- [基本サンプルの準備と一覧](../README.ja.md)
- [スケッチの組み立て方](../../../../docs/sketch-guide.ja.md)
