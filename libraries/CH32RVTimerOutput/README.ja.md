# CH32RVTimerOutput

[English](README.md)

`analogWrite()` の先のタイマー出力です。6 つのクラスがあります。

- **`CH32RVComplementaryPwm`:** 高機能タイマーのチャネルの出力と相補出力（CHx と CHxN）を、半ブリッジの上下として動かします。デッドタイムとブレーキ入力があります。
- **`CH32RVOnePulse`:** 決めた遅れのあと、決めた幅のパルスを 1 発出します（ワンパルスモード）。スケッチが始めるか、エッジでハードが始めます。
- **`CH32RVOutputCompare`:** 周期の中の決めた時刻に、端子を High・Low・反転にします（出力比較）。一致でコールバックも呼べます。
- **`CH32RVPwmTimer`:** 1 つのタイマーの複数のチャネル（と CHxN）を同じ周期の PWM にします。端揃えか中央揃え。
- **`CH32RVPhaseShift`:** 1 つのタイマーのチャネルに、同じ周波数でそれぞれの位相の矩形波（デューティ 50 %）を出します。
- **`CH32RVSixStep`:** 高機能タイマーの CH1〜CH3 と CH1N〜CH3N で、三相ブリッジの 6 ステップ（台形波）駆動をします。

```cpp
#include <CH32RVTimerOutput.h>

CH32RVComplementaryPwm bridge;
CH32RVOnePulse shot;

void setup() {
  bridge.setDeadTime(500);              // ns
  bridge.begin(PA8, PB13, 20000);       // TIM1 の CH1 と CH1N、20 kHz
  bridge.attachBreak(PB12);             // BKIN を GND に: 両方を即座にオフ
  bridge.write(128);                    // 256 のうち（8 ビット）
  shot.begin(PA6);
}

void loop() {
  shot.fire(100, 20);                   // 今から 100 us 後に 20 us の High
  delay(10);
}
```

## 端子とタイマー

- チップが持つどの経路でも、タイマーの端子を使えます。既定の端子、remap でタイマーが移る先の端子、V205・X305/X315・H41x では端子ごとの AF です（variant の `CH32RV_TIMER_PADS`、[CH32RVTimer.h](../../cores/arduino/CH32RVTimer.h) の `ch32rv_timer_pad_find()`）。いくつかのタイマーに届く端子は、空いている最初のタイマーを使います。AFIO の欄がタイマーごとに 1 つの系列では、別の経路の端子を使うと、そのタイマーの端子が全部移ります。
- タイマーは丸ごと、ていねいに借ります（[Timer resource の契約](../../docs/timer-design.ja.md)）。`analogWrite()`・`tone()`・Servo・ほかのライブラリが使っていれば、`begin()` は false です。あとでタイマーを取られたオブジェクトは止まります（`isAttached()` が false）。`end()` は端子を普通の Low の出力にします。

## CH32RVComplementaryPwm

| 呼び出し | 意味 |
|---|---|
| `begin(pin, pinN, frequency, resolution = 8)` | `pin` は高機能タイマー（TIM1、TIM8 など）のチャネルの CHx、`pinN` は同じチャネル・経路の CHxN。`resolution` は `write()` が受けるビット数（1〜16） |
| `write(duty)` | 各周期の `duty / 2^resolution` の間 CHx が High（デッドタイムの分だけ短い）。CHxN は残りの間（同じく短い）。0 で CHx が Low・CHxN が High、`2^resolution` でその逆 |
| `setDeadTime(ns)` / `deadTime()` | 切り替えの前後で両方 Low の時間。タイマーのデッドタイムの刻み（クロック単位、長いほど粗い、最大 1008 クロック: 96 MHz で 10.5 µs）に切り上げ、設定した値を返す。`begin()` の前でも後でも |
| `setDeadTime(risingNs, fallingNs)` / `deadTimeShort()` | 非対称のデッドタイム。risingNs は CHx が on になる前、fallingNs は CHxN が on になる前。長い方がタイマーのデッドタイム、短い方が 2 つ目のデッドタイム（長い方の刻みで 1 刻み以上、長い方より 1 刻み以上短い）。2 つ目のデッドタイムの無いタイマー（H41x と M030 の TIM1 以外）では、等しくなければ false。ベンチ未確認（2026-11-19） |
| `attachBreak(pin, activeLevel = LOW, mode = INPUT_PULLUP)` | タイマーのブレーキ入力（BKIN、同じ経路）。`activeLevel` になると、CPU を介さずハードが両方の出力を即座にオフ（Low）にする |
| `breakActive()` / `resume()` | ブレーキが来た（保持される）。`resume()` はブレーキ入力が戻っていれば出力を戻す。まだ有効なら false |
| `detachBreak()`、`end()`、`isAttached()` | |

両方の出力は High が有効です。ブレーキのときと `end()` のあとは両方 Low です。

## CH32RVOnePulse

| 呼び出し | 意味 |
|---|---|
| `begin(pin, polarity = HIGH, triggerPin = -1, triggerEdge = RISING)` | `pin` はパルスの端子（タイマーのチャネル）。`polarity` は `HIGH` で Low の線に High のパルス、`LOW` でその逆。`triggerPin` は同じタイマーのチャネル 1 か 2 の端子（出力と別のチャネル）で、そのエッジでハードがパルスを始める。-1 なら `fire()` だけ |
| `setPulse(delayMicros, widthMicros)` | 始めから `delayMicros`（最低 1 カウント）後に、`widthMicros` の長さ。1 カウントは、両方がカウンタに入るようにタイマーのクロックを割ったもの |
| `fire()` / `fire(delayMicros, widthMicros)` | 今 1 発始める。前のが続いている間は false |
| `busy()`、`end()`、`isAttached()` | |

## CH32RVOutputCompare

| 呼び出し | 意味 |
|---|---|
| `begin(pin, periodMicros)` | `pin` のタイマーとチャネルで、カウンタを `periodMicros` の周期で回す |
| `set(mode, atMicros)` | 各周期の `atMicros` で、端子が `mode` をする: `ACTIVE`（High）、`INACTIVE`（Low）、`TOGGLE`、すぐに `FORCE_ACTIVE` / `FORCE_INACTIVE`。`TIMING` は端子に何もしない。`TOGGLE` は周期の半分の周波数の矩形波になる |
| `onMatch(callback)` | 一致ごとにタイマーの割り込みから呼ぶ。`nullptr` で無し |
| `tickHz()`、`end()`、`isAttached()` | |

## CH32RVPwmTimer

| 呼び出し | 意味 |
|---|---|
| `begin(pin, frequency, resolution = 8, align = EDGE)` | `pin`（CHx か CHxN）のタイマーと経路で始める。`CENTER` はカウンタが上下して、どのチャネルのパルスも同じ時刻を中心にする |
| `attach(pin)` | 同じタイマー・経路の別の端子（デューティ 0 から） |
| `write(pin, duty)` | `duty / 2^resolution` を High。CHxN の端子はそのチャネルの反転なので、チャネルの値を書くことになる |
| `startWith(master)` | このタイマーを `master` の開始で始める（master の TRGO をカウンタの有効に、こちらはトリガモード）。両方のカウンタを 0 から始め直し、周期の頭がそろう。チップが内部トリガでつないでいない組では false |
| `startOn(pin, edge = RISING)` | カウンタを止め、`pin`（このタイマーの CH1 か CH2 の端子、出力に使っていないもの）のエッジを待って 0 から動かす |
| `timer()`、`end()`、`isAttached()` | |

どのタイマーがどのタイマーに従えるか（ITR の表）は device-data の `timer_itr.csv`（各系列の RM から）で、variant の `CH32RV_TIMER_ITR` になり、コアの `ch32rv_timer_trigger_from(slave, master)` が引きます。

## CH32RVPhaseShift

| 呼び出し | 意味 |
|---|---|
| `begin(pin, frequency, degrees = 0)` | `pin` のチャネルに矩形波（カウンタは周波数の 2 倍で回り、比較の一致で反転） |
| `attach(pin, degrees)` | 同じタイマー・経路の別のチャネルを `degrees` 遅れで。出力はそろって始め直す |
| `setPhase(pin, degrees)` | 位相を変える。出力はそろって始め直す（最大 1 周期の乱れ） |
| `end()`、`isAttached()` | |

## CH32RVSixStep

| 呼び出し | 意味 |
|---|---|
| `begin(uHigh, uLow, vHigh, vLow, wHigh, wLow, frequency, resolution = 8)` | U・V・W の上側（CH1〜CH3）と下側（CH1N〜CH3N）。同じ高機能タイマーと経路。始めは全部オフ |
| `write(duty)` | 上側の PWM のデューティ |
| `step(n)` | 0〜5 のステップへ（転流イベントで 3 相同時に）。6 以上は全部オフ |
| `setDeadTime(ns)`、`currentStep()`、`end()`、`isAttached()` | |

ステップ: 0 = U の PWM・V の下側オン、1 = U・W、2 = V・W、3 = V・U、4 = W・U、5 = W・V。残りの相は両方 Low。

## 状態

全系列でビルドできます（2026-11-17）。tests/instrumented/signals がプローブの取り込みで確かめます: `test_complementary`（WireSkein の dead_time で 100 MS/s のデッドタイム、BKIN に線があればブレーキ）、`test_one_pulse`、`test_output_compare`、`test_pwm_timer`（端揃えは立ち上がりの一致、中央揃えは 75 % と 25 % の位相 90 度）、`test_phase_shift`（90・270 度）、`test_timer_sync`（2 つのタイマーの立ち上がりの一致）、`test_timer_trigger`（プローブのエッジで出力が始まる）。`test_six_step` は 6 本の線がある台で動き、今のベンチには無いので、V203 で DUT 自身が 6 端子を読んで表どおりを確認しました（2026-11-19）。非対称デッドタイム（M030、H417）は未実装です。

## 例

- [HalfBridge](examples/HalfBridge/README.ja.md): デッドタイムとブレーキ入力つきの CHx / CHxN。
- [OnePulse](examples/OnePulse/README.ja.md): 1 発のパルスと、出力比較の矩形波。
