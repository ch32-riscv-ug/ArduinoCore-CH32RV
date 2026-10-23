# BoardUIAPduinoProMicro

UIAPduino Pro Micro の基板上の端子名を使って、UART・I2C・SPI・ADC・PWM を初期化します。

## 必要なものと配線

対象基板と UART モニタを用意します。I2C を接続する場合は適切な pull-up と共通 GND、SPI は指定した GPIO の CS を使います。

## 操作と期待する結果

LED・PWM の変化と UART の状態表示を確認します。SPI のループバックを試す場合は MOSI と MISO を接続します。

## API と処理の流れ

`Wire.begin(SDA, SCL)` と `SPI.begin(SCK, MISO, MOSI, -1)` では引数順が異なります。CS は GPIO として明示的に制御します。

## 改造するときの注意

メーカー図の NSS とスケッチの CS は必ずしも同じ場所ではありません。CS を変更するときは I2C・LED との競合も確認します。基板の USB 書き込み機能と、スケッチから使える USB Device 機能は区別します。

## 関連資料

- [スケッチ](BoardUIAPduinoProMicro.ino)
- [基本サンプルの準備と一覧](../README.ja.md)
- [スケッチの組み立て方](../../../../docs/sketch-guide.ja.md)
