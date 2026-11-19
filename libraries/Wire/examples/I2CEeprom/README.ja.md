# I2CEeprom

[English](README.md)

24C02 型の I2C EEPROM に文字列を書き、読み戻して `Serial` に出します。ページの境をまたぐ書き込みを分ける方法と、書き込みの内部時間を ACK のポーリングで待つ方法を示します。

## 配線と操作

EEPROM の SDA・SCL をボードの `SDA`・`SCL` に、VCC と GND を電源に。A2〜A0 は GND（アドレス 0x50）、WP は GND（書き込み可）。SDA と SCL に 4.7 kΩ ほどのプルアップ（モジュールに載っていることが多い）。`written, read back: Hello, EEPROM!` と出れば成功です。

## API と改造時の注意

書き込みは 1 回の転送でページ（24C02 は 8 バイト、24C04〜16 は 16）の中だけ。超えた分はページの先頭に回り込むので、`writeBytes()` が分けます。書いたあとの約 5 ms はアドレスに応答しないので、`endTransmission()` が 0 を返すまで繰り返します。24C32 以上はアドレスが 2 バイトなので、`Wire.write(at)` を上位・下位の 2 回にします。

## 実行前の準備

ボードと正確な Part Number を選び、端子を回路図と照合します。生成された `sketch.yaml` にビルドの profile があります。ビルドの成功は、配線や実機の動作の確認とは別です。
