# WeActCH32X035OledSH1106

[English](README.md)

WeAct CH32X035 Core Board の PC18（SDA）/ PC19（SCL）に SH1106（128×64 の OLED、I2C）をつなぎ、[U8g2](https://github.com/olikraus/u8g2)
のライブラリで文字・カウンタ・棒を描きます。

## 必要なものと配線

| SH1106 のモジュール | WeAct CH32X035 |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| SDA | PC18 |
| SCL | PC19 |

多くのモジュールはプルアップ（4.7〜10 kΩ）を載せています。無ければ SDA と SCL に付けます。アドレスは 0x3C（モジュールの
ジャンパーを変えると 0x3D、スケッチの `setI2CAddress()`）です。

U8g2 はコアに同梱していません。ライブラリマネージャで「U8g2」を入れます（確かめた版は 2.36.19。sketch.yaml のプロファイルで
ビルドすると、この版が自動で入ります）。

## 書き込みと操作

この基板の I2C は、デバッグの端子 PC18 / PC19（か USB の端子 PC16 / PC17）にしか出ていません。スケッチは
`CH32RV.releaseDebugPins()` で PC18 / PC19 を普通の端子にするので、次のリセットまで WCH-Link や OEP の probe から接続できません。

- **書き込みは USB で:** スケッチは USB のデバイス（`SerialUSB`）を動かすので、次の書き込みは基板の USB の `ch32rv-app://` の口か
  シリアルポートでできます。最初の 1 回は [WeActCH32X035](../WeActCH32X035/README.ja.md) と同じく、`WAIT_FOR_BOOT` を選んで
  書き込みを押してから、DOWNLOAD キーを押したまま USB をつなぎます。
- **期待する表示:** `Hello, SH1106!`、`WeAct CH32X035`、`uptime <秒> s` が出て、下の棒が 10 秒かけて伸びては、また始まります。

## 改造するときの注意

- `SerialUSB.begin()` を消さないでください。USB のデバイスを動かさずにデバッグの端子を手放すと、次の書き込みの手段が無くなり、
  DOWNLOAD キーでしか書き直せません。
- SSD1306 のモジュールは `U8G2_SSD1306_128X64_NONAME_1_HW_I2C` にします。
- ほかの基板向けの、端子を選べる版は Wire の例 [I2COledSH1106U8g2](../../../../Wire/examples/I2COledSH1106U8g2/README.ja.md)、
  表示のライブラリを使わない版は [I2COledSH1106](../../../../Wire/examples/I2COledSH1106/README.ja.md) です。

## 関連資料

- [スケッチ](WeActCH32X035OledSH1106.ino)
- [WeActCH32X035](../WeActCH32X035/README.ja.md)：この基板の小さい出発点と端子の対応
- [デバッグの端子を使う（`releaseDebugPins()`）](../../../README.ja.md)
