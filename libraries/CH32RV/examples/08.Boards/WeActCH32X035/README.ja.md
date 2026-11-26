# WeActCH32X035

[English](README.md)

WeAct CH32X035 Core Board のいちばん小さい出発点です。LED（PB12）を点滅させ、基板の USB-C の USB シリアル（`SerialUSB`）へ
1 秒ごとに 1 行出します。USB ケーブル 1 本で書き込んで、出力まで見られます。

## 必要なものと配線

WeAct CH32X035 Core Board（CH32X035F8U6）と USB-C ケーブルだけです。ボードは「Generic CH32X035」、Part Number は
CH32X035F8U6 を選びます。

## 書き込み

- **最初の 1 回:** port に `ch32rv-bootloader://WAIT_FOR_BOOT` を選んで「書き込み」を押し、待ちに入ってから、DOWNLOAD キーを
  押したまま USB をつなぎます（factory USB ISP は、書き込みが来ないと約 10 秒でアプリに戻るため、書き込みを押した後に入れます）。
- **2 回目から:** このスケッチが動いていれば、port の一覧に `ch32rv-app://…`（表示は「CH32RV Arduino (CH32RV, USB)」）が
  出ます。それを選んで「書き込み」を押すだけで、DOWNLOAD キーなしで書けます。同じ USB のシリアルポート（`COMx`、
  `/dev/ttyACM…`）は、モニター用です。

詳しくは [書き込み経路](../../../../../docs/upload-routes.ja.md) を見てください。

## 操作と期待する結果

LED が、短く 2 回光ってから休む、を 1 秒ごとに繰り返します。シリアルモニターで、USB のシリアルポート（または `ch32rv-app://` の口）を開くと、
`hello from WeAct CH32X035 0`、`1`、… と 1 秒ごとに出ます。

## 基板の端子

| 機能 | 端子 | 注意 |
|---|---|---|
| LED | PB12 | High で点灯 |
| UART（`Serial`） | TX PA2、RX PA3 | USART2 |
| UART（`Serial1`） | TX PB0、RX PB1 | USART4 |
| SPI | SCK PA5、MISO PA6、MOSI PA7、CS PA4 | CS は通常の GPIO |
| ADC | A0 / PA0、A1 / PA1 | |
| USB データ | D− PC16、D+ PC17 | `SerialUSB` が使う |
| USB PD | CC1 PC14、CC2 PC15 | [CH32RVUsbPd](../../../../CH32RVUsbPd/README.ja.md) |
| デバッグ | PC18 / PC19 | WCH-Link・OEP probe をつなぐ端子 |

I2C は、USB データの端子かデバッグの端子にしか出ていません。UART・SPI・ADC・PWM・PD・I2C をまとめて使う形は
[WeActCH32X035Peripherals](../WeActCH32X035Peripherals/README.ja.md) にあります。

## 関連資料

- [スケッチ](WeActCH32X035.ino)
- [基本サンプルの準備と一覧](../../README.ja.md)
- [USB シリアルの例（CH32RVUsbDevice）](../../../../CH32RVUsbDevice/examples/README.ja.md)
- [サンプルの案内](../../../../../docs/examples.ja.md)
