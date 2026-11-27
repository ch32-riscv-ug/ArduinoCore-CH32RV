# I2COledSH1106U8g2

[English](README.md)

SH1106（128×64 の OLED、I2C）に、[U8g2](https://github.com/olikraus/u8g2) のライブラリで文字・カウンタ・棒を描きます。フォントや
図形が使えるので、実際の表示に近い形です。表示のライブラリを使わない版は [I2COledSH1106](../I2COledSH1106/README.ja.md) です。

## 必要なものと配線

SH1106 の I2C のモジュールを、VCC を 3.3 V、GND、SDA と SCL をスケッチの端子へつなぎます（プルアップとアドレスは
[I2COledSH1106](../I2COledSH1106/README.ja.md) と同じ）。

U8g2 はコアに同梱していません。ライブラリマネージャで「U8g2」を入れます。この例の sketch.yaml には、確かめた版（2.36.19）を
書いてあるので、プロファイルでビルドすると、その版が自動で入ります。

## 操作と期待する結果

`Hello, SH1106!`、`CH32RV + U8g2`、`uptime <秒> s` が出て、下の棒が 10 秒かけて満ちてはまた始まります。

## API と処理の流れ

U8g2 は、このコア（ESP32 以外）では引数なしの `Wire.begin()` を呼びます。そのため、先に `Wire.setPins(SDA, SCL)` で端子を
選んでおきます（Wire はその端子で開きます）。アドレスは 1 ビットずらして `setI2CAddress(0x3C << 1)` と渡します。

ページの形（`_1_`）は 128 バイトのバッファで 1 ページずつ描くので、`firstPage()` / `nextPage()` の繰り返しの中で毎回同じ絵を
描きます。V003（RAM 2 KB）でも入ります（V003 で flash 13.2 KB・RAM 464 B）。

## 改造するときの注意

- RAM に余裕がある石では、`U8G2_SH1106_128X64_NONAME_F_HW_I2C`（画面全体 1 KB）にすると、`clearBuffer()` で描いて
  `sendBuffer()` で一度に送れます。
- SSD1306 のモジュールは `U8G2_SSD1306_128X64_NONAME_1_HW_I2C` にします。
- **WeAct CH32X035（F8U6）**で PC18 / PC19 を使うときは、[I2COledSH1106](../I2COledSH1106/README.ja.md) と同じく、コメントの
  端子に切り替えて `USE_DEBUG_PADS` を 1 にします（USB のデバイスを動かすので、次の書き込みも USB でできます）。

## 関連資料

- [スケッチ](I2COledSH1106U8g2.ino)
- [I2COledSH1106](../I2COledSH1106/README.ja.md)：表示のライブラリを使わない版
- [I2CScanner](../I2CScanner/README.ja.md)：モジュールのアドレスを調べる
- [ライブラリの API](../../README.ja.md)
