# CH32RVLogicCapture

[English](README.md)

GPIO のポート 1 つ(16 本の入力)を、DMA でまとめて RAM に取り込むライブラリです。ロジックアナライザの入口に使えます。
CH32H415 / H416 / H417 用です。

```cpp
#include <CH32RVLogicCapture.h>

CH32RVLogicCapture logic;
static uint16_t buf[32768];
logic.begin(PORT_E, 10000000);              // ポートと、1 秒あたりのサンプル数
logic.startContinuous(buf, 32768);          // 16384 ずつの半分を交互に埋める
if (const uint16_t *half = logic.readBuffer()) { ... }   // loop() から

uint32_t rate = logic.captureBurst(buf, 32768);   // 1 回だけ、速く
```

各サンプルは、そのポートの入力レジスタの値です(bit n が Pxn)。端子のモードに関係なく読めます。

## 2 つの方式(CH32H417、HCLK 100 MHz での実測、2026-10-21)

| | 連続(`startContinuous()`) | 1 回取り(`captureBurst()`) |
|---|---|---|
| 仕組み | TIM6 の更新で DMA2 のチャネル 8 が読む | タイマーなしで DMA が続けて読む |
| 速さ | F_CPU ÷ 8 = 12.5 MS/s まで | 1 サンプル 5 クロック = 20 MS/s |
| 時間の正確さ | タイマーで決まり、正確 | DMA の速さで決まる。バスの取り合いで揺れる |
| 長さ | 循環バッファで、いつまでも | 65535 サンプルまで |

**連続の方式**
- ポートからの DMA の転送は 1 回に HCLK の 8 クロックかかり、その最中に来た要求は捨てられます。そのため上限は
  F_CPU ÷ 8 で、`begin()` はそこで止めます。
- 上限以下なら、サンプルは遅れずに取れます。同じポートで PWM を取り込んで確かめたところ、周期はサンプル単位で正確でした。

**1 回取りの方式**
- 間隔はタイマーではなく DMA の速さで決まります。CPU や別の DMA が同時にバスを使うと、サンプルが 1 クロック遅れることがあります。
  16 ビットで約 320 周期を取ったうち、6 か所が数サンプルずれました。
- 実行中は割り込みを止めます。

## そのほか

- TIM6 は、コアのタイマー管理(`CH32RVTimer.h`)を通して借ります。`analogWrite()` / `tone()` / Servo と取り合いません。
- 連続の方式で `readBuffer()` が返した半分は、次の `readBuffer()` まで使えます。DMA がそこへ戻ってくるまでに処理を済ませてください。
  時間は「半分のサンプル数 ÷ 速さ」です。スケッチが受け取る前に次の回が来た半分は `overruns()` に数え、そのデータは失われます。
- バッファは、DMA が届く RAM(グローバル変数や static)に置いてください。
- USB の SuperSpeed で流す例は `LogicStream` です。12.5 MS/s × 2 バイト = 25 MB/s で送ります。
- 入力の電圧は、そのポートの電源の電圧までです。PE7〜PE15 などは VIO18 の端子です(CH32RVHsAdc の README の
  「入力の範囲」を参照)。
