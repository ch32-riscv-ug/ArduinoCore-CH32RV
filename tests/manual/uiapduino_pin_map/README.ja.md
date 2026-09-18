# UIAPduino Pro Micro CH32V003 V1.4 ピン対応

`esp32-d0wd-v3-0070070d9394`（host側 `/dev/ttyUSB0`）とUIAPduinoの
実配線を、V003側から各padを単独でHIGHにし、ESP32側を入力として観測した結果です。
測定日は2026-09-18です。

| V003 pad | Arduino名 | ESP32 GPIO | 備考 |
|---|---:|---:|---|
| PA1 | A1 | 25 | |
| PA2 | A0 | 26 | |
| PC0 | D2 | 5 | 基板LED接続 |
| PC1 | SDA | 19 | |
| PC2 | SCL | 18 | |
| PC3 | D5 | 17 | |
| PC4 | A2 / SS | 33 | |
| PC5 | SCK | 4, 27 | 左右ヘッダの重複配線 |
| PC6 | MOSI | 2, 12 | 左右ヘッダの重複配線 |
| PC7 | MISO | 14, 15 | 左右ヘッダの重複配線 |
| PD0 | D10 | 13 | |
| PD1 | D11 / SWIO | 16 | 既知配線。mapping sketchでは触らない |
| PD2 | A3 | 32 | |
| PD5 | A5 / TX | 22 | ESP32 UART RX候補 |
| PD6 | A6 / RX | 21 | ESP32 UART TX候補 |
| PD7 | RESET | 23 | 既知配線。常時Hi-Z、mapping sketchでは触らない |

PD3/A4とPD4/A7はUSB D+/D-なのでmapping対象外です。ESP32 GPIO0、34、35、36、39には、
この測定で対象にした外部ヘッダGPIOからの接続は検出されませんでした。

## 再測定

V003側は[`uiapduino_pin_map.ino`](uiapduino_pin_map.ino)をこのコアでbuildし、製品HID
bootloaderから書き込みます。ESP32側は`wch-protocols`の
`experiments/e132_uiapduino_pin_map/e132_uiapduino_pin_map.ino`です。serial command `G`で
32秒観測します。

V003側は全対象HIGHを1秒出すframe markerの後、表の順に1本ずつ500 ms HIGH、250 ms LOWを
繰り返します。ESP32の出力で、markerを挟んだ2周以上が同じ順序になることを合格条件にします。
SWIO、RESET、USB D+/D-は対象配列に含めません。
