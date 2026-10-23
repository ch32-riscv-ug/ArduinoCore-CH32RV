# AnalogResolution

[English](README.md)

ADC と PWM の数値のスケールを変える使い方を示します。

## 必要なものと配線

`A1` に可変抵抗を接続し、`PWM_PIN` には抵抗付き LED または測定器を接続します。端子は基板へ合わせます。

## 操作と期待する結果

UART に8 bitと12 bitでの読み取り値が出ます。PWM は指定分解能の最大値と0を交互に出力します。

## API と処理の流れ

`analogReadResolution(bits)` と `analogWriteResolution(bits)` は、入出力値の数値範囲を設定します。読み取り値はそれぞれ別の変換なので、入力が変動すると厳密な倍数にはなりません。

## 改造するときの注意

数値の幅を増やしても ADC の物理的精度が増すわけではありません。PWM の周波数設定とも別です。ほかの処理と組み合わせるときは、分解能の設定を途中で変える影響も考えます。

## 関連資料

- [スケッチ](AnalogResolution.ino)
- [基本サンプルの準備と一覧](../README.ja.md)
- [スケッチの組み立て方](../../../../docs/sketch-guide.ja.md)
