# CH32RVHsAdc

[English](README.md)

CH32H415 / H416 / H417 の高速 ADC(HSADC)を使うライブラリです。10 ビットで、最大 20 Msps です。

| 端子 | チャネル | 電源 |
|---|---|---|
| PC0、PC1、PC2、PC3 | 0〜3 | VIO18 |
| PF8、PF9、PF10 | 4〜6 | VDDIO(H415 には出ていない) |

```cpp
#include <CH32RVHsAdc.h>

CH32RVHsAdc hsadc;
hsadc.begin(PF8, 20000000);     // 端子と、1 秒あたりのサンプル数
uint16_t v = hsadc.read();      // 1 回の変換、0〜1023

static uint16_t a[4096], b[4096];
hsadc.startContinuous(a, b, 4096);              // DMA が a、b、a … を交互に埋める
if (const uint16_t *full = hsadc.readBuffer()) {   // loop() から
    // full[0..4095]
}
```

## 速さ

- HSADC のクロックは PLL_CLK(400 MHz)÷(分周 + 1)で、上限は 100 MHz です。1 回の変換に 5 クロックかかります。
- 選べるのは 20 Msps から 1.25 Msps までの飛び飛びの値で、`begin()` は指定以下でいちばん近いものを選びます。
  実際の値は `sampleRate()` で読めます。
- CH32H417 での実測(2026-10-21)では、20.0 Msps で連続して取り込み、取りこぼしは 0 でした。端子を 3.3 V に
  したときの値は 1022〜1023 です。

## 入力の範囲(大事)

- 入力の範囲は、0 V からその端子の電源の電圧までです。
- **PC0〜PC3 は VIO18 の端子です。** 評価ボードの VIO18 は 1.2 V で起動し、入力の絶対最大定格は VIO18 + 0.3 V です。
- 0〜3.3 V を入れる前に、VIO18 を 3.3 V にしてください。ボードメニューの **VIO18 Supply** か、
  `CH32RV.setVio18Millivolts(3300)` で設定できます。ただし、VIO18 に 1.8 V の部品がつながっている基板では上げないでください。
- PF8〜PF10 は VDDIO(3.3 V)の端子なので、この設定は要りません。

## 連続の取り込み

- 2 つのバッファを交互に使います。`readBuffer()` が返したバッファは、次に `readBuffer()` を呼ぶまでスケッチのものです。
- DMA は、もう一方のバッファを埋め終わると、そのバッファに戻って書き込みます。それまでに処理を済ませてください。
  時間は「サンプル数 ÷ サンプリング周波数」で、16384 サンプルを 20 Msps なら 819 µs です。
- スケッチが受け取る前に次の回が来たバッファは、`overruns()` に数えます。そのバッファのデータは失われます。
- バッファは、DMA が届く RAM(ふつうのグローバル変数や static の配列)に置いてください。V5F の TCM は使えません。
- USB の SuperSpeed で流す例は `HsAdcStream` です。20 Msps × 2 バイト = 40 MB/s を Vendor クラスの
  `writeDirect()` で送ります。PC 側は、libusb の非同期転送をいくつも並べて読んでください。同期で 1 回ずつ
  読むと、読み出しの合間にバッファを取りこぼします。

## サンプルの詳しい使い方

- [HsAdcRead](examples/HsAdcRead/README.ja.md)：配線・操作・API の使い方と改造時の注意。
- [HsAdcStream](examples/HsAdcStream/README.ja.md)：配線・操作・API の使い方と改造時の注意。
