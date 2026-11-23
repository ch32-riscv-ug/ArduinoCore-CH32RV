# CH32RVLowPowerTimer

[English](README.md)

低消費電力タイマー（LPTIM）のライブラリです。CH32L103 / M103 に 1 個、CH32H41x に 2 個あります。16 bit のカウンターで、LSI・LSE で数えれば `lightSleep()`（Stop）の間も動き、石を起こせます。端子のパルスを、クロックを全部止めたまま数えることもできます。arduino-esp32 に相当するものはありません。

```cpp
#include <CH32RV.h>
#include <CH32RVLowPowerTimer.h>

CH32RVLowPowerTimer lpt;                    // LPTIM1（H41x の LPTIM2 は lpt2(2)）

void setup() {
  lpt.begin(CH32RVLowPowerTimer::CLOCK_LSI, 32);   // LSI / 32 で数える
  lpt.setPeriod(lpt.clockHz() / 4);         // 250 ms ごとに
  lpt.onPeriod(tick);                       // タイマーの割り込みで呼ぶ
  lpt.start();
  CH32RV.enableExtiLineWakeup(lpt.wakeLine());   // lightSleep() が周期で終わる
}
```

| 関数 | 意味 |
|---|---|
| `begin(clock, divider)` | 内部のクロックで数える。`CLOCK_BUS`（PCLK1。Stop では止まる）、`CLOCK_HSI`、`CLOCK_LSE`（動いているときだけ。RTC Clock メニューの LSE）、`CLOCK_LSI`（ここで起動。公称値で、数十 % ずれる）。divider は 1・2・4〜128 |
| `beginCounter(pin, edge, asynchronous, clock)` | CH1 の端子の縁を数える（すぐ始まる）。既定は内部のクロックで標本化（縁を落とさないが、そのクロックの速さまで）。asynchronous は端子そのものをタイマーのクロックにし、クロックが全部止まっても数えるが、start() の後の最初の縁を 5 つまで数えない（RM 17.3.9。H417 では 2〜3）。CHANGE（両縁）は標本化のときだけ |
| `beginEncoder(pinA, pinB, clock)` | CH1 / CH2 の直交エンコーダー（両方の全部の縁、1 周期 4 カウント）。0〜周期 − 1 を上下する。clock は縁の 4 倍以上 |
| `setPeriod(ticks)` | カウンターが ticks（1〜65536、既定 65536）ごとに 0 に戻る |
| `setCompare(ticks)` | onCompare() と出力の縁の値（周期より小さく。設定するまでは周期の最後の値） |
| `output(pin, inverted)` | タイマーの出力を OUT の端子に。比較値から周期の終わりまで High（inverted で Low）。start() で PWM、startOnce() で 1 発 |
| `start()` / `startOnce()` / `stop()` | 繰り返し／1 周期だけ／止めて 0 に |
| `count()` | 今の値（タイマーのクロックはバスと別なので、同じ値を 2 回続けて読むまで読む） |
| `direction()` | エンコーダーの今の向き。1 上、−1 下 |
| `onPeriod(cb)` / `onCompare(cb)` | 周期の終わり／比較値で、タイマーの割り込みの中で呼ぶ。設定している間だけ割り込み（と起床）が有効 |
| `clockHz()` | 1 秒の tick 数（begin() のクロック / divider。非同期の数えるときは 0） |
| `wakeLine()` | `CH32RV.enableExtiLineWakeup()` に渡す EXTI の線。周期の終わりと比較値の一致で lightSleep() が終わる（ハードの起床は onPeriod() / onCompare() の設定に関係なく両方から出る） |
| `end()` | 止めて返す |

- 端子は variant の `CH32RV_LPTIMn_PADS` にあります。H41x は端子ごとに機能を選ぶのでどれでも、L103 は AFIO の route（PB12〜PB15 か PB5〜PB7・PB2）が同じものだけです。
- ARR と CMP の書き込みはタイマーのクロックへ送られ、次の書き込みはそれが届く（ARROK / CMPOK）のを待ちます（RM 17.3.8）。LSI / 128 のように遅いクロックでは setPeriod() が数 ms かかることがあります。非同期の数えでは端子が動くまで届かないので、5 ms で待つのをやめます。
- 1 つのタイマーは 1 つのオブジェクトだけが使えます。
- CH32H417 で測った癖（2026-11-23）: CFGR の CLKMX_SEL（クロックの選択）は、タイマーが有効の間に値が変わったときだけ効きます（無効の間に書いても、有効にしてから同じ値を書き直しても PCLK1 のまま）。起床の線（EXTI）は CFGR の TIMOUT が 1 のときだけ出て、比較値の一致でも（割り込みを有効にしていなくても）出ます。ライブラリはどちらも中で扱います。

## 例

- [LowPowerTick](examples/LowPowerTick/README.ja.md): lightSleep() で眠り、LSI の LPTIM に 250 ms ごとに起こしてもらう。

## 試験

periph::test_lptim: ラベル付きの線のうち LPTIM の CH1 の端子（DUT が lptfind で探す）にプローブの PWM を入れ、バスのクロックの周期の数、LSI の速さ、標本化と非同期の数え（1 kHz・10 kHz）、LSI の周期での lightSleep() からの起床を DUT が数えた値で判定する。H417 で通る（LPTIM2、PB10。LSI は約 42.3 kHz、200 ms の周期で 189 ms 眠った。2026-11-23）。L103 はベンチの線が LPTIM の端子に無く、ビルドのみ。
