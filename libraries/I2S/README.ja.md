# I2S

[English](README.md)

SPI の I2S モード（I2S2・I2S3）で音声を出し入れするライブラリです。CH32V30x、CH32V4x7、CH32H41x にあります。CH32H417 の SAI（標準・TDM・SPDIF 出力）、CH32H41x の DFSDM の PDM マイクにも対応します。API は arduino-esp32 の ESP_I2S と同じ形です（ヘッダーは `I2S.h`、クラスは `I2SClass`）。

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
| `setFormat(format)` | `CH32RVI2SFormat::Philips`（既定）、`_MSB`（左詰め）、`_LSB`（右詰め）、`_PCM_SHORT`、`_PCM_LONG`。begin() の前に |
| `setPinsPdmRx(clk, din0)` | PDM マイク（`I2S_MODE_PDM_RX`）。CH32H41x の DFSDM を使う。clk は CKOUT、din0 は DATIN の端子（variant の `CH32RV_DFSDM_PADS`） |
| `setInverted(bclk, ws)` | BCLK の極性を反転、WS は左右を入れ替え |
| `setBufferSize(bytes)` | DMA バッファーの半分の大きさ（既定 1024 バイト）。begin() の前に |
| `begin(I2S_MODE_STD、I2S_MODE_TDM（SAI）、I2S_MODE_SPDIF_TX（SAI）か I2S_MODE_PDM_RX, rate, bits, slots, slot_mask = -1)` | bits は 16・24・32、slots は `I2S_SLOT_MODE_MONO` / `_STEREO` |
| `write(buf, n)` / `availableForWrite()` | 出力 |
| `readBytes(buf, n)` / `read()` / `available()` | 入力。readBytes() は `setTimeout()`（既定 1 秒）まで待つ |
| `flush()` | 書いた音が出終わるまで待つ |
| `sampleRate()` | 分周器で実際に出る速さ |
| `underruns()` / `overruns()` | 出力が間に合わなかったバッファーの半分の数／入力で読み落とした数 |
| `lastError()` | `I2S_ERROR_ARG`（速さ・幅）、`_PINS`、`_DMA`、`_MEMORY`、`_STATE` |
| `end()` | 止めて端子を入力に戻す |

- サンプルのメモリー上の形は ESP_I2S と同じです。リトルエンディアンで、ステレオは左・右の順。16 bit は `int16_t`、24 bit は 3 バイト、32 bit は `int32_t` です。モノラルの出力は slot_mask のスロット（既定は両方）に同じ値を、入力は左（`I2S_STD_SLOT_RIGHT` なら右）を読みます。
- コントローラー（マスター）側だけ、標準（2 スロット）モードだけです。SPI の I2S には TDM と PDM がありません（H417 の SAI は別）。PDM マイクは代わりに CH32H41x の DFSDM を使います（`I2S_MODE_PDM_RX`、下記）。SPI の I2S は半二重なので、1 つのインスタンスは出力か入力のどちらかです。両方を同時に使うときは I2S2 と I2S3 で 2 つ作ります。
- 端子で I2S2 か I2S3 かが決まります。使える端子は variant の `CH32RV_I2S2_PADS` / `CH32RV_I2S3_PADS` にあります。H41x は端子ごとに機能を選べるのでどの組み合わせでも使えます。V30x・V4x7 は AFIO の route（SPI と同じフィールド）が同じものだけです。
- クロックは SYSCLK を 2 x I2SDIV + ODD（I2SDIV は 2〜255）で割ったものです。速さは頼んだものからわずかにずれ、`sampleRate()` で分かります。分周器で作れない速さ（16 bit で SYSCLK / 16352 未満。144 MHz なら 8.8 kHz、H41x の 400 MHz なら 24.5 kHz）は begin() が false です。
- 出力は DMA が 2 つの半分を交互に流すバッファーです。間に合わなかった半分は無音になるので、書くのをやめれば音も止まります。入力は DMA が埋める輪のバッファーで、読まずに置いたものは上書きされます（`overruns()`）。
- DMA の半分・全部の割り込みはこのライブラリが持ちます（SPI2・SPI3 の DMA チャネルの割り込み）。
- 同じ SPI を SPI ライブラリと同時に使うことはできません。

### SAI（CH32H417）

```cpp
i2s.setPins(PF8, PF9, PA13);                // SCK_B、FS_B、SD_B: SAI のブロック B（FS は -1 で外に出さない）
i2s.begin(I2S_MODE_TDM, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, 0x0F);   // スロット 0〜3
int16_t frame[4] = {a, b, c, d};
i2s.write((const uint8_t *)frame, sizeof(frame));
```

- 端子が SAI のブロックのもの（variant の `CH32RV_SAI_PADS`: ブロック A か B の SCK・FS・SD・MCLK）なら SAI を使います。SPI の I2S の端子でもある場合、標準モードは SPI を選びます。出力も入力も、標準モード（上の形式）と `I2S_MODE_TDM` に対応します。
- TDM: `slot_mask` は使うスロットです（ESP_I2S と同じ `int8_t` で、スロット 0〜6。-1 はスロット 0、ステレオなら 0 と 1）。枠は 2・4・8 スロット（使う最大より後ろは止める）で、16 bit のサンプルなら 16 bit、24・32 bit なら 32 bit ずつ。メモリーには使うスロット 1 つにつき 1 サンプル。Philips は枠の前半で FS が Low（1 ビット早く）、PCM は 1 か 13 ビットのパルスです。
- クロック: SAI_CK = SYSCLK / 6、MCLK = SAI_CK / MCKDIV（1〜63）= 256 fs なので、速さは粗めです。400 MHz では 32 kHz → 32552 Hz、16 kHz → 16276、44.1 kHz → 43403、48 kHz → 52083（`sampleRate()` で分かる）。FS は要らなければ出さなくて（-1）かまいません。
- SPDIF（`I2S_MODE_SPDIF_TX`。ESP_I2S には無い）: `setPins(-1, -1, sd)` で、ブロック A か B の SD の端子だけを使います（RM が SCK・FS・MCLK を放すよう求めている）。IEC 60958 の 2 サブフレームのフレームで、サンプルの上位 24 bit（16 bit のサンプルは上の 16 bit）、validity は 0、チャネル状態は民生用でコピー可、頼んだ速さの標本化周波数のコード（44.1・48・32・88.2・96・176.4・192 kHz。ほかは「示さない」）。SAI の parity はサンプルの 24 ビットだけで計算されるので（H417 で実測。RM 36.3.8 は 27 ビットと書いている）、user のビットをチャネル状態のビットと同じにして parity を偶数に保ちます。user のビットを読む受信機には、そこにチャネル状態と同じ並びが見えます。ビットのセルは同じ分周（64 fs）から作るので、速さは上と同じく粗めです。出力だけです。
- DMA のチャネルは使いません。SAI の FIFO の割り込みでサンプルを運びます（両ブロックで 1 つの割り込み）。CH32H417 で I2S を使うスケッチには SAI のコード（約 3 KB）が入ります。

### PDM マイク（CH32H41x）

```cpp
i2s.setPinsPdmRx(PE9, PD6);                 // CKOUT をマイクの CLK へ、マイクの DATA を DATIN1 へ
i2s.begin(I2S_MODE_PDM_RX, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
int16_t frame[2];
i2s.readBytes((char *)frame, sizeof(frame));   // 左、右
```

- SPI ではなく DFSDM（ライブラリ [CH32RVDfsdm](../CH32RVDfsdm/README.ja.md)）を使います。クロックは 64 x rate（HCLK の分周。H417 の HCLK 100 MHz で 16 kHz を頼むと 15943 Hz。`sampleRate()` で分かる）、sinc3・1/64 のフィルターで、幅のフルスケールに合わせます。
- ステレオは din0 の 1 本に 2 つのマイクです。左は CLK の立ち上がり、右は立ち下がりで送ります（マイクの L/R か SELECT の端子）。モノラルは左、`I2S_STD_SLOT_RIGHT` なら右を読みます。
- 線は 1 本だけです（din1〜din3 は -1）。setPinsPdmRx() は別のファイルにあり、呼ばないスケッチにはこれも CH32RVDfsdm もリンクされません。PDM の出力（`I2S_MODE_PDM_TX`）はありません。

## 例

- [SineWave](examples/SineWave/README.ja.md): I2S の DAC に正弦波を流す。

## 試験

spi::test_i2s: 出力を 3 本のラベル付きの線で取り込み、WireSkein の i2s で判定（16・32 bit の Philips、24 bit の左詰め・右詰め、16 bit モノラル。8 フレームの乱数が続けて出ること、32 kHz の 1 % 以内、スロットの BCLK 数）。spi::test_i2s_input: プローブの PWM を SD に入れ、DUT が読んだスロットの 1・0 の割合と立ち上がりの数を PWM と比べる。H417 で通る（2026-11-23）。V30x・V4x7 はビルドのみ（ベンチの線が I2S の組み合わせにならない）。spi::test_sai: SAI のブロック B の SCK と SD（FS はベンチの線に無い）で、SCK が sampleRate() x 枠のビット数の方形波（16 bit 標準、TDM 4 x 16 と 8 x 32）を WireSkein で判定し、入力はプローブの PWM を test_i2s_input と同じく読む（H417、2026-11-23）。SD のビット列を一度解読し、サンプルが順に全部出ていることも確かめた。periph::test_i2s_pdm_rx: `I2S_MODE_PDM_RX` の 16 kHz。プローブの PWM でデータの列を作り、デューティー 1/4・3/4 でモノラルとステレオの両スロットの平均が 32767 の -1/2・+1/2、オーバーランなし、CKOUT は WireSkein で判定（H417、2026-11-23）。
