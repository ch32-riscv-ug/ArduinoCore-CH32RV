# I2S

[English](README.md)

SPI の I2S モード（I2S2・I2S3）で音声を出し入れするライブラリです。CH32V30x、CH32V4x7、CH32H41x にあります。API は arduino-esp32 の ESP_I2S と同じ形です（ヘッダーは `I2S.h`、クラスは `I2SClass`）。

```cpp
#include <I2S.h>

I2SClass i2s;

void setup() {
  i2s.setPins(PB13, PB12, PB15);            // BCLK, WS, DOUT（入力なら setPins(bclk, ws, -1, din)）
  i2s.begin(I2S_MODE_STD, 48000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
}

void loop() {
  int16_t frame[2] = {left, right};
  i2s.write((const uint8_t *)frame, sizeof(frame));   // バッファーが一杯の間は待つ
}
```

| 関数 | 意味 |
|---|---|
| `setPins(bclk, ws, dout, din = -1, mclk = -1)` | 端子。dout なら出力、din なら入力（どちらか一方）。mclk は 256 fs のマスタークロック |
| `setFormat(format)` | `I2S_FORMAT_PHILIPS`（既定）、`_MSB`（左詰め）、`_LSB`（右詰め）、`_PCM_SHORT`、`_PCM_LONG`。begin() の前に |
| `setInverted(bclk, ws)` | BCLK の極性を反転、WS は左右を入れ替え |
| `setBufferSize(bytes)` | DMA バッファーの半分の大きさ（既定 1024 バイト）。begin() の前に |
| `begin(I2S_MODE_STD, rate, bits, slots, slot_mask = -1)` | bits は 16・24・32、slots は `I2S_SLOT_MODE_MONO` / `_STEREO` |
| `write(buf, n)` / `availableForWrite()` | 出力 |
| `readBytes(buf, n)` / `read()` / `available()` | 入力。readBytes() は `setTimeout()`（既定 1 秒）まで待つ |
| `flush()` | 書いた音が出終わるまで待つ |
| `sampleRate()` | 分周器で実際に出る速さ |
| `underruns()` / `overruns()` | 出力が間に合わなかったバッファーの半分の数／入力で読み落とした数 |
| `lastError()` | `I2S_ERROR_ARG`（速さ・幅）、`_PINS`、`_DMA`、`_MEMORY`、`_STATE` |
| `end()` | 止めて端子を入力に戻す |

- サンプルのメモリー上の形は ESP_I2S と同じです。リトルエンディアンで、ステレオは左・右の順。16 bit は `int16_t`、24 bit は 3 バイト、32 bit は `int32_t` です。モノラルの出力は slot_mask のスロット（既定は両方）に同じ値を、入力は左（`I2S_STD_SLOT_RIGHT` なら右）を読みます。
- コントローラー（マスター）側だけ、標準（2 スロット）モードだけです。SPI の I2S には TDM と PDM がありません（H417 の SAI は別）。SPI の I2S は半二重なので、1 つのインスタンスは出力か入力のどちらかです。両方を同時に使うときは I2S2 と I2S3 で 2 つ作ります。
- 端子で I2S2 か I2S3 かが決まります。使える端子は variant の `CH32RV_I2S2_PADS` / `CH32RV_I2S3_PADS` にあります。H41x は端子ごとに機能を選べるのでどの組み合わせでも使えます。V30x・V4x7 は AFIO の route（SPI と同じフィールド）が同じものだけです。
- クロックは SYSCLK を 2 x I2SDIV + ODD（I2SDIV は 2〜255）で割ったものです。速さは頼んだものからわずかにずれ、`sampleRate()` で分かります。分周器で作れない速さ（16 bit で SYSCLK / 16352 未満。144 MHz なら 8.8 kHz、H41x の 400 MHz なら 24.5 kHz）は begin() が false です。
- 出力は DMA が 2 つの半分を交互に流すバッファーです。間に合わなかった半分は無音になるので、書くのをやめれば音も止まります。入力は DMA が埋める輪のバッファーで、読まずに置いたものは上書きされます（`overruns()`）。
- DMA の半分・全部の割り込みはこのライブラリが持ちます（SPI2・SPI3 の DMA チャネルの割り込み）。
- 同じ SPI を SPI ライブラリと同時に使うことはできません。

## 例

- [SineWave](examples/SineWave/README.ja.md): I2S の DAC に正弦波を流す。

## 試験

spi::test_i2s: 出力を 3 本のラベル付きの線で取り込み、WireSkein の i2s で判定（16・32 bit の Philips、24 bit の左詰め・右詰め、16 bit モノラル。8 フレームの乱数が続けて出ること、32 kHz の 1 % 以内、スロットの BCLK 数）。spi::test_i2s_input: プローブの PWM を SD に入れ、DUT が読んだスロットの 1・0 の割合と立ち上がりの数を PWM と比べる。H417 で通る（2026-11-23）。V30x・V4x7 はビルドのみ（ベンチの線が I2S の組み合わせにならない）。
