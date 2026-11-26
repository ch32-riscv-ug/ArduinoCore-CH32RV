# UIAPduinoProMicro

[English](README.md)

UIAPduino Pro Micro の LED を、基板に印刷された番号（2 = PC0）で点滅させます。USB をつなぐだけで書き込んで、そのまま動きます。

## 必要なものと配線

UIAPduino Pro Micro CH32V003 V1.4 と USB ケーブルだけです。ボードは「UIAPduino Pro Micro CH32V003 V1.4」、port は基板の
USB（HID のブートローダー）を選んで書き込みます。

## 操作と期待する結果

基板の LED が、短く 2 回光ってから休む、を繰り返します。

## この基板の USB について

基板の USB は、書き込みのためのブートローダーのものです。スケッチが動いている間、USB は何もしません（CH32V003 に USB の
周辺機能はありません）。そのため、USB だけでは `Serial` の出力は見えません。ほかの機能を試すときは、次のどちらかを足します。

- **UART:** USB-UART の変換器を TX = 15（PD5）・RX = 16（PD6）・GND につなぎ、`Serial.begin(115200)` で使います。
- **デバッグの線:** WCH-LinkE を SWIO（11 = PD1）・GND につなぐと、`SerialSDI` や `SerialDMSeq` で、UART の配線なしに
  読めます（[デバッグ出力](../../../../../docs/debug-output.ja.md)）。

## 基板の端子

印刷の番号（0〜17）、`D0`〜`D17`、端子名（`PC0` など）は、どれで書いても同じ端子に届きます。

| 機能 | 印刷の番号 / 端子 |
|---|---|
| LED | 2 / PC0、High で点灯 |
| UART | TX 15 / PD5、RX 16 / PD6 |
| I2C | SDA 3 / PC1、SCL 4 / PC2 |
| SPI | SCK 7 / PC5、MISO 9 / PC7、MOSI 8 / PC6 |
| ADC | A0 = 1 / PA2、A1 = 0 / PA1、A2 = 6 / PC4 |
| PWM の例 | 12 / PD2 |
| 基板が使う端子 | USB 13 / PD3・14 / PD4（ブートローダー）、SWIO 11 / PD1、RESET 17 / PD7 |

基板が使う端子は、書き込みと復旧のために使わないでください。メーカーの図にある SPI の NSS の候補（PC1 / PC0）は、I2C の SDA と
LED に重なります。SPI の CS は、空いている GPIO（たとえば 5 / PC3）を使います。

## 関連資料

- [スケッチ](UIAPduinoProMicro.ino)
- [基本サンプルの準備と一覧](../../README.ja.md)
- [サンプルの案内](../../../../../docs/examples.ja.md)
- [スケッチの組み立て方](../../../../../docs/sketch-guide.ja.md)
