# BoardUIAPduinoProMicro

[English](README.md)

UIAPduino Pro Micro の基板上の端子名を使って、UART・I2C・SPI・ADC・PWM を初期化します。

## 必要なものと配線

対象基板と UART モニタを用意します。I2C を接続する場合は適切な pull-up と共通 GND、SPI は指定した GPIO の CS を使います。

## 操作と期待する結果

LED・PWM の変化と UART の状態表示を確認します。SPI のループバックを試す場合は MOSI と MISO を接続します。

## API と処理の流れ

`Wire.begin(SDA, SCL)` と `SPI.begin(SCK, MISO, MOSI, -1)` では引数順が異なります。CS は GPIO として明示的に制御します。

## 改造するときの注意

メーカー図の NSS とスケッチの CS は必ずしも同じ場所ではありません。CS を変更するときは I2C・LED との競合も確認します。基板の USB 書き込み機能と、スケッチから使える USB Device 機能は区別します。

### 基板上の端子割り当て

雛形の対象はUIAPduino Pro Micro CH32V003 V1.4です。下の別名はこの基板のもので、Generic V003の基板配置を示すものではありません。

| 機能 | pad / 基板表記 |
|---|---|
| LED | PC0 / D2、Highで点灯 |
| UART | TX PD5 / D15、RX PD6 / D16 |
| I2C | SDA PC1 / D3、SCL PC2 / D4 |
| SPI | SCK PC5 / D7、MISO PC7 / D9、MOSI PC6 / D8 |
| GPIOのCS | PC3 / D5 |
| ADC | A0 PA2 / D1、A1 PA1 / D0、A2 PC4 / D6 |
| PWM | PD2 / D12 |
| デバッグ / reset | SWIO PD1 / RESET PD7 |

メーカー図のNSS候補PC1はI2CのSDA、PC0はLEDと重なります。どちらかをCSに使うなら、置き換える機能を外します。CSはGPIOで制御するので、hardware NSSと同じ端子である必要はありません。USB bootloaderはPD3/PD4を使いますが、その配線だけでアプリのUSB Device対応を意味するものではありません。

## 関連資料

- [スケッチ](BoardUIAPduinoProMicro.ino)
- [基本サンプルの準備と一覧](../README.ja.md)
- [スケッチの組み立て方](../../../../docs/sketch-guide.ja.md)
