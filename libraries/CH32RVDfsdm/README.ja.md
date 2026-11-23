# CH32RVDfsdm

[English](README.md)

CH32H41x の DFSDM（ΣΔ 変調器のデジタルフィルター）です。1 ビットの列（PDM マイク、絶縁型の ΣΔ 変調器）を sinc フィルターでサンプルにします。Arduino・arduino-esp32 に対応するものはありません（arduino-esp32 の PDM マイクは I2S の周辺機能を通します）。

```cpp
#include <CH32RVDfsdm.h>

CH32RVDfsdm mic;                                   // フィルター 0（CH32RVDfsdm(1) でフィルター 1）

void setup() {
  mic.begin(PE9, PD6, 2000000, 64);                // CKOUT でマイクにクロック、データは DATIN1。2 MHz / 64
}

void loop() {
  int32_t s = mic.read();                          // 31.25 kHz のサンプル、-fullScale() .. fullScale()
}
```

| 関数 | 意味 |
|---|---|
| `begin(clockPin, dataPin, clockHz, oversample = 64, order = 3, integrator = 1)` | クロックの端子: CKOUT の端子（DFSDM が変調器にクロックを出す。HCLK / 2 .. 256 のうち clockHz 以下で最も近いもの）か、そのチャネルの CKIN の端子（変調器が自分のクロックを出す。clockHz は sampleRate() の計算に使うだけ）。データの端子: DATIN0 か DATIN1。これでチャネルが決まる |
| `read()` / `read(buf, n)` | 次のサンプル（待つ）／リングにある最大 n 個 |
| `available()` | リングにあるサンプルの数 |
| `clock()` / `sampleRate()` | CKOUT の速さ／clock / (oversample x integrator) |
| `fullScale()` | 1 が続くと +fullScale()、0 が続くと -fullScale() |
| `overruns()` | 失ったサンプル（リングが満杯、またはフィルター自身のオーバーラン） |
| `setBuffer(n)` / `setFallingEdge(on)` | `begin()` の前に: リングの大きさ（既定 64）／SCK の立ち下がりでデータを取る |
| `end()` | 止めて端子を入力に戻す |

- フィルター: sinc の次数 1 .. 5（0 は FastSinc）、オーバーサンプリング 1 .. 1024、そのあと積分器で 1 .. 256 個を足します。fullScale() は oversample ^ order x integrator（FastSinc は 2 x oversample ^ 2 x integrator）で、24 ビットに収まるよう右にシフトします。フィルターの 32 ビットを超えるときは begin() が false です。
- フィルター 0 と 1 はそれぞれ 1 チャネルを使い、同時に動かせます。データの端子が 2 つでも 1 つでもよく、1 つのときは 2 つ目のフィルターがもう一方のチャネルを通して読みます（1 本の線に 2 つのマイク。2 つ目は `setFallingEdge(true)`）。CKOUT は共通なので、2 つ目の begin() は同じ速さを頼んでください。チャネルの設定は DFSDM 全体を止めている間しか書けないため、2 つ目の begin() で 1 つ目のフィルターが一瞬止まります（落ち着くまでの数サンプルを失います）。
- サンプルはフィルターの割り込み（DFSDM0 / DFSDM1、実行時に付ける）でヒープの int32_t のリングに入ります。
- 端子は variant の `CH32RV_DFSDM_PADS` です。アナログウォッチドッグ、最大・最小の検出、短絡とクロック断の検出、注入変換とそのトリガー、CPU や ADC からの並列入力、DMA は対象外です。

## 例

- [PdmLevel](examples/PdmLevel/README.ja.md): PDM マイクの RMS とピーク。

## 試験

periph::test_dfsdm: CKOUT（2 MHz、WireSkein で判定）と DATIN。データの列はプローブの PWM で作ります。High 保持で +64^3、Low 保持で -64^3 ちょうど（sinc3、オーバーサンプリング 64）、10 kHz・デューティー 1/4 と 3/4 で平均がその -1/2 と +1/2、サンプルが sampleRate() で来てオーバーランなし。フィルター 1 だけ、1 本の線を 2 つのフィルターで（立ち上がりと立ち下がり）、プローブの 1 MHz を CKIN のクロックにした場合も見ます。H417 で合格（PE9 / PD6 / PD7、2026-11-23）。マイクはつないでいません。H415 / H416 はビルドだけです。
