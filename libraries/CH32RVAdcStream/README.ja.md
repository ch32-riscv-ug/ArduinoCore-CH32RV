# CH32RVAdcStream

[English](README.md)

ADC1 を一定のレートで（自走、タイマーの刻み、端子の縁で）回し続け、結果を DMA で 2 面のバッファへ交互に入れるライブラリです。`analogRead()` を何度も呼ぶ代わりに、変換は止まらずに進み、スケッチは埋まった半分を受け取って処理します。API の形は H41x の高速 ADC 用の [CH32RVHsAdc](../CH32RVHsAdc/README.ja.md) と同じです。

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

- **`begin()`: ADC は自走します。** 連続変換モードで、変換が終わると次の変換が始まります。タイマーは使いません。
- **レートは段で決まります。** ADC のクロックとサンプル時間の 8 段の組み合わせで、変換の速さが決まります。段ごとの速さは系列で違い、表もないので、`begin()` が 8 段を一度ずつ実際に回して測り、頼んだレート以下で一番速い段を選びます。`sampleRate()` は、その測った値です。
- **一番遅い段より遅くはできません。** 例: CH32V307（96 MHz）で 1 秒あたり約 47,600 回、CH32X035 で約 250,000 回、CH32V407（200 MHz）で約 99,000 回、CH32H417 で約 198,000 回（2026-10-27 に測定）。それより遅いレートを頼むと、一番遅い段になります。ゆっくりした一定のレートは `beginTimer()` で。
- **`beginTimer()`: タイマーの刻みごとに 1 巡。** その系列の ADC を起動できるタイマー（F1 型の系列は TIM3 の TRGO、V00x / X035 は TIM2・TIM1 の TRGO、TRGO の無い M030 などはチャネルのコンペア。どれを取ったかは `triggerTimer()`）のうち空いたものを、`analogWrite()` や `timerBegin()` と同じタイマーの管理から取ります。空きが無ければ false です。レートはタイマーのクロックを整数で割った値で、割り切れれば正確、いくらでも遅くできます（自走の下限より下、1 Hz 未満も）。サンプル時間は、1 巡が刻みの 80 % に収まる一番長い段です（`beginTimer()` が段ごとに一度測る）。`sampleRate()` はタイマーのレートです。
- **`beginExternal()`: 端子の縁ごとに 1 巡**（`RISING`・`FALLING`・`CHANGE`）。端子はその系列の ADC のトリガの端子で、多くの系列はビット 11 のどの端子でも（PA11・PB11・PC11 …）、X305 / X315 はビット 12、V00x は PD3 か PC2、M030 は PA14 か PB6 です（`isExternalTriggerPin()`）。V00x の端子は `CHANGE` でもパルスごとに 1 巡しか起動しないので、`CHANGE` は受け付けません（`isExternalTriggerPin(pin, CHANGE)` が false）。縁は端子の EXTI の線をイベントとして通ります。同じ端子の `attachInterrupt()` も動き、縁の設定を共有します。`maxRate`（1 秒あたりに来る縁の数の上限）でタイマーと同じようにサンプル時間を選びます。省くと一番長い段。`sampleRate()` は 0 です。
- **`conversions()`** は `startContinuous()` からの巡の数です。n 番目の巡は `buffer[(n * 端子の数) % (2 * samples)]` にあるので、半分が埋まるのを待たずに、トリガの 1 巡ずつを見られます。
- **端子は 1〜6 本。** 複数のときは順番に変換し、バッファには端子 0、端子 1、…、端子 0、端子 1、… と並びます。`sampleRate()` は 1 巡を 1 と数えます。半分の長さは端子の数の倍数にします。
- **値は変換器のままです**（12 ビットの系列で 0〜4095、10 ビットで 0〜1023）。`analogRead()` の幅の換算はかけません（変換器の幅なら `analogRead()` と同じ値です。L103 / M103 のオフセットの補正は `analogReadCalibrated()` がかけます）。

## 部品

| 操作 | 意味 |
|---|---|
| `begin(pin, rate)` / `begin(pins, count, rate)` | 自走。ADC1 の端子でないものがあれば false。呼び直して端子やレートを変えられる |
| `beginTimer(pin, rate)` / `beginTimer(pins, count, rate)` | 1 秒に `rate` 回のタイマーの刻みごとに 1 巡。ADC1 の端子でない、トリガのタイマーに空きが無い、レートが 0 かタイマーの届かない値なら false |
| `beginExternal(pin, triggerPin, edge[, maxRate])` / `beginExternal(pins, count, triggerPin, edge[, maxRate])` | `triggerPin` の縁ごとに 1 巡。トリガの端子でなければ false |
| `isExternalTriggerPin(pin)` | その端子がこの石で `beginExternal()` のトリガになれるか |
| `conversions()` / `triggerTimer()` | `startContinuous()` からの巡の数。`beginTimer()` が取ったタイマー（番号） |
| `startContinuous(buffer, samples)` | `buffer` は `samples` の 2 倍の長さ。前半と後半を交互に埋める |
| `startContinuous(buf0, buf1, samples)` | CH32RVHsAdc と同じ形。buf1 は buf0 の直後に続くこと（1 本の配列の前半と後半） |
| `readBuffer()` | 埋まった半分。無ければ nullptr。次の呼び出しまでスケッチのもの |
| `overruns()` | スケッチが受け取る前に、DMA がまた来て上書きした半分の数 |
| `buffersFilled()` / `sampleRate()` / `sampleTimeStep()` | 埋まった半分の数、選んだ段の速さ（測定値）、段（0 が最速） |
| `stop()` / `end()` | 止める。`end()` はタイマーかトリガの端子を返し、ADC を `analogRead()` 用の設定に戻す |

取り込みの間は `analogRead()` を使わないでください。`end()` のあとは使えます。ADC1 は 1 つなので、このライブラリと arduino-esp32 の `analogContinuous()`（コアの Arduino.h）は同時には動きません。

## アナログウォッチドッグ

```cpp
adc.setWatchdog(0, 1000);          // 0〜1000 の外に出たら旗（-1: 全部の端子、0..: その端子だけ）
if (adc.watchdogTripped()) { ... } // 旗を読んで消す
adc.clearWatchdog();
```

ADC のハードが変換のたびに比べるので、スケッチは何もしなくてよく、取りこぼしもありません。値は buffer と同じ
変換器の単位です。割り込みは使いません（旗を読む）。`setWatchdog()` を呼ぶスケッチだけにリンクされます。
V203・V003・H417 で、プローブが線を Low / High に保ったとき、範囲の外のときだけ旗が立つことを確かめました
（2026-11-18、periph::test_adc_watchdog）。

## 注入変換（CH32RVAdcInjected）

```cpp
#include <CH32RVAdcInjected.h>

CH32RVAdcInjected inj;
const pin_size_t pins[] = {PA0, PA1};
inj.begin(pins, 2);                    // 1〜4 本、この順に 1 回の変換で
uint16_t v[2];
inj.convert(v);                        // 今 1 回（ソフトの開始）、待って値を返す
inj.onRound([](const uint16_t *v, uint8_t n) { /* ADC の割り込みから */ });
inj.startTimer(1000);                  // タイマーで 1 秒に 1000 回
```

ADC1 の注入グループです。`analogRead()` やストリームの通常の変換の合間に割り込んで、決めた瞬間に最大 4 本を
変換します（PWM の周期の決まった位置でモーターの電流を取る、など）。値は変換器の生の数（`bits()` ビット、
`analogReadResolution()` は効かない）。`startTimer()` のタイマーは、チップが注入グループを起動できる源
（variant の `CH32RV_ADC_JTRIGGERS`、device-data の adc_triggers.csv から生成: タイマーの TRGO か、チャネルの
比較の一致）のうち空いているもので、`rate()` が実際の回数を返します。`value(i)`・`rounds()` で最後の値と回数。
V203・X035 で、プローブが線を Low / High に保ったときの値と、1 kHz で 500 ms に 499 回を確かめました
（2026-11-19、periph::test_adc_injected）。

## サンプル

- [AdcStreamStats](examples/AdcStreamStats/README.ja.md): 1 本の端子を取り込み、1 秒ごとに数、最小・最大・平均、取りこぼしを表示します。
- [AdcTriggered](examples/AdcTriggered/README.ja.md): 端子をタイマーの刻みで、次に別の端子の立ち上がりで変換します。
