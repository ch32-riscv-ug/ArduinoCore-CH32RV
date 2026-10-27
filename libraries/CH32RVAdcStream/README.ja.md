# CH32RVAdcStream

[English](README.md)

ADC1 を一定のレートで回し続け、結果を DMA で 2 面のバッファへ交互に入れるライブラリです。`analogRead()` を何度も呼ぶ代わりに、変換は止まらずに進み、スケッチは埋まった半分を受け取って処理します。API の形は H41x の高速 ADC 用の [CH32RVHsAdc](../CH32RVHsAdc/README.ja.md) と同じです。

```cpp
#include <CH32RVAdcStream.h>

CH32RVAdcStream adc;
static uint16_t buffer[2 * 512];

void setup() {
  adc.begin(A0, 100000);                  // 端子、1 秒あたりの変換の数（これ以下で一番速い段を選ぶ）
  adc.startContinuous(buffer, 512);       // DMA が前半、後半、前半 ... と埋める
}

void loop() {
  if (const uint16_t *full = adc.readBuffer()) {   // 埋まった半分。次の readBuffer() まで使ってよい
    // full[0] .. full[511]
  }
}
```

## 対象

ADC1 の DMA が DMA1 のチャネル 1 につながる系列です（ch32-device-data の dma.csv）。CH32V003 / V006、V103、V20x、V205、V30x、L103 / M103、M030、X033 / X035、X305 / X315 の ADC1 の端子。V407 / V467 は DMA1 のチャネル 12 で、使い方は同じです（V407 の 200 MHz で確認）。H415 / H416 / H417 は DMAMUX で DMA1 のチャネル 1 につなぎます（H417 で確認）。

## 考え方

- **ADC は自走します。** 連続変換モードで、変換が終わると次の変換が始まります。タイマーは使いません。
- **レートは段で決まります。** ADC のクロックとサンプル時間の 8 段の組み合わせで、変換の速さが決まります。段ごとの速さは系列で違い、表もないので、`begin()` が 8 段を一度ずつ実際に回して測り、頼んだレート以下で一番速い段を選びます。`sampleRate()` は、その測った値です。
- **一番遅い段より遅くはできません。** 例: CH32V307（96 MHz）で 1 秒あたり約 47,600 回、CH32X035 で約 250,000 回、CH32V407（200 MHz）で約 99,000 回、CH32H417 で約 198,000 回（2026-10-27 に測定）。それより遅いレートを頼むと、一番遅い段になります。ゆっくりした一定のレートが要るときは、n 個に 1 個を使うか、タイマーから `analogRead()` を呼びます。
- **端子は 1〜6 本。** 複数のときは順番に変換し、バッファには端子 0、端子 1、…、端子 0、端子 1、… と並びます。`sampleRate()` は 1 巡を 1 と数えます。半分の長さは端子の数の倍数にします。
- **値は変換器のままです**（12 ビットの系列で 0〜4095、10 ビットで 0〜1023）。`analogRead()` の幅の換算や校正の補正はかけません。

## 部品

| 操作 | 意味 |
|---|---|
| `begin(pin, rate)` / `begin(pins, count, rate)` | ADC1 の端子でないものがあれば false。呼び直して端子やレートを変えられる |
| `startContinuous(buffer, samples)` | `buffer` は `samples` の 2 倍の長さ。前半と後半を交互に埋める |
| `startContinuous(buf0, buf1, samples)` | CH32RVHsAdc と同じ形。buf1 は buf0 の直後に続くこと（1 本の配列の前半と後半） |
| `readBuffer()` | 埋まった半分。無ければ nullptr。次の呼び出しまでスケッチのもの |
| `overruns()` | スケッチが受け取る前に、DMA がまた来て上書きした半分の数 |
| `buffersFilled()` / `sampleRate()` / `sampleTimeStep()` | 埋まった半分の数、選んだ段の速さ（測定値）、段（0 が最速） |
| `stop()` / `end()` | 止める。`end()` は ADC を `analogRead()` 用の設定に戻す |

取り込みの間は `analogRead()` を使わないでください。`end()` のあとは使えます。

## サンプル

- [AdcStreamStats](examples/AdcStreamStats/README.ja.md): 1 本の端子を取り込み、1 秒ごとに数、最小・最大・平均、取りこぼしを表示します。
