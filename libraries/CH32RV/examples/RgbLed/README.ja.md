# RgbLed

[English](README.md)

`rgbLedWrite()`（arduino-esp32 と同じ名前）で、WS2812 / SK6812 型の RGB LED をどの端子からでも光らせます。

## 必要なものと配線

1 個目の LED のデータ入力（DIN）を `LED_PIN`（PA1）に、LED の GND を板の GND につなぎます。LED の電源はデータシートのとおりにします。5 V の WS2812B は High に 0.7 x VDD 以上を求めるので、3.3 V の端子からはレベル変換（74AHCT125 など）を入れるか、1 個目の LED の電源をダイオード 1 本分下げます。DIN の近くに数百 Ω の直列抵抗を入れるのが普通です。

## 操作と期待する結果

チェーンが 0.5 秒ごとに赤・緑・青・消灯を繰り返します。チェーンの先の LED ほど、1 つ手前の半分の明るさです。

## API と処理の流れ

`rgbLedWrite(pin, r, g, b)` は LED 1 個分の 24 ビットを GRB の順で送り（割り込みを約 30 µs 止める）、戻ります。チェーンは `ch32rv_rgb_led_write(pin, bytes, len)` で、全部の LED のバイト（送る順）を 1 つのフレームで送ります。`rgbLedWrite()` を続けて呼ぶと、LED の間で線が呼び出しの間 Low になり、遅いクロックではそれがラッチになってしまいます（V006 の 24 MHz で 13 µs）。どちらもラッチは待ちません。線が Low のままリセットの時間（WS2812 で 50 µs、WS2812B-V5 など新しい部品で 280 µs）を過ぎると色が出ます。ほかの色の順は `rgbLedWriteOrdered(pin, LED_COLOR_ORDER_RGB, r, g, b)` です。`neopixelWrite()` は古い名前で、非推奨です。

## 改造するときの注意

チェーンの全体で、LED 1 個につき 30 µs 割り込みが止まります（100 個で 3 ms）。CH32V4x7 と CH32X3x5 では、`LED_PIN` を WS2812 のブロックの端子（PA15。X3x5 は PD3・PD4 も）にすると、同じ関数がブロックと DMA で送り、割り込みを止めません（`ch32rv_rgb_led_is_hardware(LED_PIN)` で分かる）。ほかの系列で LED が多いときは、SPI や DMA で線を動かすライブラリなら割り込みを止めません。タイミングはソフトで作っています。新しい板で色がおかしいときは、ロジックアナライザで波形を確かめます。

## 関連資料

- [スケッチ](RgbLed.ino)
- [基本サンプルの準備と一覧](../README.ja.md)
- [サンプルの案内](../../../../docs/examples.ja.md)
- [スケッチの組み立て方](../../../../docs/sketch-guide.ja.md)
