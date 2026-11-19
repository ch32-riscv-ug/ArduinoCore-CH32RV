# CH32RVDac

[English](README.md)

`analogWrite()` の先の DAC です。DAC 自身が作る三角波とノイズ、DMA が送るサンプルの再生を、タイマーのトリガ（TRGO）で
1 段ずつ進めます。出している間 CPU は何もしません。V30x、V4x7、H41x（DAC のある系列）で使えます。

```cpp
#include <CH32RVDac.h>
CH32RVDac dac(1);                  // DAC1（CH32RV_DAC1_PIN: PA4）
dac.begin();
dac.triangle(12, 100000);          // 0〜4095 を往復、10 us ごとに 1 段
dac.noise(8, 50000);               // 変換器の LFSR の 8 bit、50 kHz
dac.play(sine, 64, 64000);         // 64 サンプル、1 kHz、繰り返し
dac.write(2048);                   // 普通の値に戻す（波形は止まる）
```

## API

| 関数 | 内容 |
|---|---|
| `begin()` / `end()` | 端子を DAC にして出力バッファを入れ、0 を出す / 波形を止めて変換器を切る |
| `write(value)` | 0〜4095 を出す。波形が動いていれば先に止める |
| `triangle(bits, stepHz, base)` | base〜base + 2^bits - 1 を 1 LSB ずつ往復。12 bit の全幅なら周波数は stepHz / 8190 |
| `noise(bits, stepHz, base)` | 変換器の LFSR の下位 bits を base に足した値を、毎秒 stepHz 回 |
| `play(samples, count, sampleHz)` | 12 bit の右詰めのサンプルを DMA で繰り返し出す。配列は再生中ずっと有効にしておく（書き換えればそのまま出る） |
| `stop()` | 波形を止める。出力は最後の値のまま |
| `stepRate()` / `running()` | タイマーが実際に作る 1 秒あたりの段数（近い値）/ 動いているか |

## 仕組みと制約

- 1 段の速さは、DAC が受けられるトリガのタイマー（variant の `CH32RV_DAC_TRIGGERS`。TIM6、TIM7 を先に、次に TIM8、
  TIM5、TIM2、TIM4）を 1 つ借りて作ります。波形が動いている間、そのタイマーは丸ごと使います。
- `play()` は variant の `CH32RV_DACn_DMA` の DMA チャネル（V30x / V4x7 は DMA2 の ch3 / ch4、H41x は DMAMUX）を、
  再生中だけ使います。
- 2 つのチャネルはそれぞれ別のタイマーで動きます。同じトリガで同時に更新する（DHR12RD）形はまだありません。
- DAC の無い系列では、どの関数も false を返します。

## 試験

`analog::test_dac_waves`（tests/instrumented/analog）が、三角波・ノイズ・サンプルの再生を、同じ石の ADC で読み戻して
範囲・変化の数・タイマーの速さを判定します。V407 と H417 で通っています（2026-11-19）。
