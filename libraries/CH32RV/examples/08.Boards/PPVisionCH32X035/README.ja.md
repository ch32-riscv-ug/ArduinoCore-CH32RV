# PPVisionCH32X035

[English](README.md)

[ppvision/CH32X035](https://github.com/ppvision/CH32X035) の基板です。CH32X035G8U6（QFN28）に、0.96 インチ 160×80 の
ST7735 の液晶、横のボタン 2 つ、LED 4 つが載っています。スケッチは
[Adafruit の ST7735 のライブラリ](https://github.com/adafruit/Adafruit-ST7735-Library)で液晶に描き、ボタンを押した回数を
数えて表示し、LED を順に光らせます。

## 端子

回路図は公開されていません。下の端子は基板のサンプルのソース（`Apps/gpio`、`Apps/FreeRTOS`、`components/lcd`）から
読んだものです。LED とボタンの基板上の位置は確かめていません。

| 機能 | 端子 | 出どころ |
|---|---|---|
| 液晶の SCK / MOSI（SPI1） | PA5 / PA7 | `components/lcd`（コアの既定の SPI の端子） |
| 液晶の CS / DC / RST | PB4 / PB1 / PB0 | `components/lcd`。バックライトの制御は無い |
| ボタン UP / DOWN（GND へ、プルアップ） | PB8 / PB6 | `Apps/gpio` の EXTI、立ち下がり |
| LED | PA0、PA1、PC0、PC3 | `Apps/gpio` が出力にして点滅させる |
| SPI フラッシュの CS（この例では使わない） | PB3 | `SPI_CS2` のコメント |
| ヘッダーの TX2 / RX2 | PA2 / PA3（USART2） | `Apps/SimulateCDC` |

液晶は 160×80 の「plugin」のパネルです。基板のドライバーと同じく、オフセット 26 / 1、BGR、反転なしにします。

Adafruit のライブラリはコアに同梱していません。ライブラリマネージャで「Adafruit ST7735 and ST7789 Library」を入れます
（Adafruit GFX と BusIO も入ります。確かめた版は 1.12.0 / 2.0.0 / 1.17.4。sketch.yaml のプロファイルでビルドすると、
この版が自動で入ります）。

## 書き込みと操作

- **ボードメニュー:** CH32X035、Part Number は CH32X035G8U6。
- **書き込み:** 3V3 / GND / DIO / DCK のパッド（SWDIO PC18、SWCLK PC19）に WCH-LinkE をつなぎます。スケッチは USB の
  デバイス（`SerialUSB`）を動かすので、次からは基板の USB の `ch32rv-app://` の口かシリアルポートでも書き込めます。
- **読み出し保護:** この基板は出荷時に読み出し保護が掛かっていることがあります。最初の書き込みが
  `error[target-protected]: the chip is read-protected` で止まったら、ツール → Remove Read Protection Before Sketch Upload を
  Enabled にして書き込み直します。保護を外すと flash が全部消え、そのあとスケッチを書きます。書けたら Disabled に戻します。
  USB のブートローダーから書くときは BOOT キーを 2 回使います。保護の解除はリセットの後に効くので、ch32rv が
  「put the chip in its bootloader again now」と案内したら、もう一度 BOOT キーを押したままリセットします（ch32rv 0.38.0 では
  1 回目が照合で失敗するので、ブートローダーに入れ直してもう一度書き込みます）。
- **期待する表示:** 上に `CH32X035 + ST7735`、中央に数、右に UP / DOWN の枠が出て、ボタンを押している間は枠が塗られます。
  UP で 1 増え、DOWN で 1 減り、変わるたびに USB のシリアルポートへ出します。4 つの LED が 200 ms ごとに順に光ります。

## 改造するときの注意

- 色が反転して見えたら（黒が白になる）、`tft.invertDisplay(true)` を `false` にします。絵が数ドットずれるときは別の種類の
  パネルなので、`INITR_MINI160x80` を試します。
- `tft.setRotation(1)` で上下が逆になります。0 / 2 は縦（80×160）です。

## 関連資料

- [スケッチ](PPVisionCH32X035.ino)
- [ppvision/CH32X035](https://github.com/ppvision/CH32X035)：基板のソースと写真
