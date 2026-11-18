# CH32RVTimerInput

[English](README.md)

タイマーを入力に使います。4 つのクラスがあります。

- **`CH32RVPulseCapture`:** 信号の周期と High の時間（パルス幅）を測ります。
- **`CH32RVEncoder`:** 直交のロータリーエンコーダを数えます。
- **`CH32RVInputCapture`:** 信号のエッジ 1 つずつを、その時刻と一緒に渡します。
- **`CH32RVPulseCounter`:** 信号のパルスを数えます（タイマーの外部クロックモード 1）。

はじめの 2 つは、数えるのはタイマーです。スケッチは、ほしいときに結果を読むだけです。割り込みからスケッチの関数を呼ぶことはありません。読みに行かなくても、ハードは数え続けます。`CH32RVInputCapture` はエッジを全部残します。割り込みがキューに入れるか、コールバックに渡します。

```cpp
#include <CH32RVTimerInput.h>

CH32RVPulseCapture pulse;
CH32RVEncoder knob;
CH32RVInputCapture edges;

void setup() {
  Serial.begin(115200);
  pulse.begin(PA6);                    // タイマーのチャネル 1 か 2 の端子
  knob.attachFullQuad(PB6, PB7);       // 1 つのタイマーのチャネル 1 と 2
  edges.begin(PB0, CHANGE);            // タイマーのどのチャネルでも
}

void loop() {
  CH32RVPulseCapture::Pulse p;
  if (pulse.read(p)) {                 // 前の read() から後の、最新の 1 周期
    Serial.println(p.frequencyHz());
  }
  Serial.println((long)knob.getCount());
  CH32RVInputCapture::Edge e;
  while (edges.read(e)) {              // 全部のエッジを古い順に
    Serial.println((unsigned long)e.ticks);   // 既定は µs
  }
}
```

## 端子とタイマー

- 使える端子は、チップが持つどの経路でも、タイマーのチャネルの端子です。既定の端子、remap でタイマーが移る先の端子、V205・X305/X315・H41x では端子ごとの AF です（variant の `CH32RV_TIMER_PADS`、[CH32RVTimer.h](../../cores/arduino/CH32RVTimer.h) の `ch32rv_timer_pad_find()`）。`CH32RVPulseCapture` と `CH32RVEncoder` はチャネル 1 か 2 です（スレーブのリセットとエンコーダモードがこの 2 つにつながっているため）。`CH32RVInputCapture` はどのチャネルでも使えます。いくつかのタイマーに届く端子は、空いている最初のタイマーを使います。
- それ以外の端子では、`begin()` / `attach*()` が false を返します。エンコーダの A と B は、同じタイマーの同じ経路のチャネル 1 と 2 でなければなりません。
- AFIO の欄がタイマーごとに 1 つの系列では、別の経路の端子をつなぐと、そのタイマーの端子が全部移ります。
- タイマーは丸ごと借ります（[Timer resource の契約](../../docs/timer-design.ja.md)）。`analogWrite()`、`tone()`、Servo が先に使っていれば、false を返します。
- あとから `analogWrite()` などが同じタイマーを取ると、こちらは止まります。`read()` は false、`getCount()` は最後の値、`isAttached()` は false になります。
- タイマーの 1 カウントは、タイマーのクロック（`ch32rv_timer_clock_hz()`）を各クラスが選ぶプリスケーラで割ったものです。ストップとスタンバイの間はタイマーも止まり、その間のエッジは数えません。

## CH32RVPulseCapture

タイマーの PWM 入力モードを使います。立ち上がりでカウンタを 0 に戻して周期を捕まえ、立ち下がりで High の時間を捕まえます。この 2 つの値は、ハードが毎周期レジスタに入れ直します。

| 呼び出し | 意味 |
|---|---|
| `begin(pin, minHz = 10, mode = INPUT)` | `minHz` は、測る最も遅い信号です。これで 1 カウントの時間（分解能）が決まります。例: 16 ビットのタイマーで、48 MHz なら 10 Hz で 1.5 µs、1 kHz で 21 ns。`mode` は `INPUT` / `INPUT_PULLUP` / `INPUT_PULLDOWN` |
| `read(pulse)` | 前の `read()` から後の、最新の 1 周期。無ければ false。`begin()` の直後の 1 周期と、止まったあとの最初の周期は捨てます（カウンタが立ち上がりから始まっていないため） |
| `stalled()` | `1 / minHz` の間、立ち上がりが無かった。線が止まっている（`digitalRead()` で High か Low か分かる）か、`minHz` より遅い。`read()` が次に周期を返すまで true |
| `tickHz()` | 1 秒あたりのカウント数 |
| `setFilter(cycles)` | 約 `cycles`（F_CPU のサイクル、0〜1024）より短いパルスを無視する。雑音のある線に |
| `end()` | タイマーを返す |

`Pulse` の中身: `period` と `high`（カウント）、`tickHz`。換算の関数として `periodMicros()`、`highMicros()`、`frequencyHz()`（整数）、`frequency()`、`duty()`（float）があります。float の関数は、使わなければリンクされません。

- 周期は「立ち上がりから次の立ち上がりまで」です。信号が始まる前に雑音などのエッジがあると、最初の 1 回はそこからの時間になります。
- 「最新の 1 周期」なので、`read()` の間にあった周期は捨てられます。平均がほしいときは、何回か読んで平均してください。

## CH32RVEncoder

タイマーのエンコーダモードを使います。名前は ESP32Encoder と同じです。

| 呼び出し | 意味 |
|---|---|
| `attachFullQuad(a, b)` | A と B の全部のエッジ。1 周期で 4 カウント |
| `attachHalfQuad(a, b)` | A の両方のエッジ。1 周期で 2 カウント |
| `attachSingleEdge(a, b)` | 1 周期で 1 カウント（半分の数え方を 2 で割ったもの） |
| `getCount()` | 64 ビットの数。A が B より先に変わると増えます |
| `setCount(v)` / `clearCount()` | 数を設定する |
| `pauseCount()` / `resumeCount()` | 数えるのを止める・再開する。止めている間のエッジは数えない |
| `setPull(mode)` | `attach*()` の前に。既定は `INPUT_PULLUP`（GND への接点のエンコーダ向け） |
| `setFilter(cycles)` | 約 `cycles` より短いパルスを無視する。接点のチャタリングに |
| `detach()` / `isAttached()` | タイマーを返す / 借りているか |

- `attach*()` は 0 から数え始めます。
- 16 ビットのカウンタを越える分は、タイマーの更新割り込みで、ライブラリの中で数えます。
- カウンタの端（0 と 65535 の境）で、割り込みが間に合わないほど速く行き来すると（数 µs で 2 回）、65536 ずれるおそれがあります。チャタリングのある接点には `setFilter()` を使ってください。

## CH32RVPulseCounter

```cpp
CH32RVPulseCounter pulses;
pulses.begin(PA0);                 // チャネル 1 か 2 の端子。RISING（既定）、FALLING、CHANGE（チャネル 1 だけ）
pulses.clear();
delay(1000);
uint64_t hz = pulses.count();      // 1 秒に数えた数
```

信号の縁でタイマーのカウンタそのものを進めます（外部クロックモード 1、TI1FP1 / TI2FP2 / TI1F_ED）。縁ごとに割り込みは
動かないので、タイマーのクロックの半分ほどまでのパルスを数えられます。`count()` は 64 ビットで、カウンタのあふれは
エンコーダと同じく更新割り込みで足します。`setFilter()` で短いパルスを捨てられます。ETR の端子はまだ使えません
（コアの端子の表に ETR が無い）。V203・V205・H417 で、プローブの 20 kHz を 200 ms に 1 % 以内で数えることを確かめました
（2026-11-18、signals::test_pulse_counter）。

## CH32RVInputCapture

エッジの時刻は、ハードの捕捉がその瞬間のカウントを押さえます。割り込みが遅れても、時刻は 1 カウントまで正確です。割り込みはカウントを 64 ビットに延ばし、エッジをキュー（8 個まで）に入れるか、スケッチのコールバックを呼びます。

| 呼び出し | 意味 |
|---|---|
| `begin(pin, edge = CHANGE, tickHz = 1000000, mode = INPUT)` | `edge` は `RISING` / `FALLING` / `CHANGE`。`tickHz` はカウントの速さで、タイマーのクロックを整数で割ったもの（いちばん近いものを使う）。既定の 1 MHz なら 1 カウントが 1 µs。端子がタイマーのチャネルでない、届くタイマーが全部使用中、`tickHz` が届かないときは false |
| `read(edge)` / `available()` | キューのいちばん古いエッジ。`ticks`（64 ビット）と `rising` |
| `onEdge(callback)` | キューの代わりに、エッジごとにタイマーの割り込みから順に呼ぶ。`const CH32RVInputCapture::Edge &` を受ける関数か小さなラムダ。短く。`nullptr` でキューに戻る |
| `lost()` | 落としたエッジの数。キューがいっぱいだったか、同じ向きのエッジが割り込みより速く 2 つ来た |
| `now()` | 今のカウント（エッジと同じ尺度） |
| `setFilter(cycles)` | `begin()` の前に。タイマーのクロックで約 `cycles` より短いパルスを無視する |
| `end()` / `isAttached()` | タイマーを返す / 借りているか |

- `CHANGE` は 2 つのチャネルを使います。端子のチャネルが立ち上がりを、そのペア（1-2、3-4）が同じ入力の立ち下がりを押さえるので、立ち上がりと立ち下がりが上書きし合うことはありません。どちらでもタイマーは丸ごと借ります。
- 同じ向きのエッジが数 µs 以内に 2 つ来ると、新しい方を残し、もう一方を `lost()` に数えます。

## timerPulseIn()

`timerPulseIn(pin, state, timeout = 1000000)` は、タイマーの捕捉で測る `pulseIn()` です。引数と結果（幅の µs、期限切れで 0）は同じで、両方のエッジをハードが押さえるので、パルスの間に割り込みが入っても幅は 1 µs まで正確です。端子はどの経路でもタイマーのチャネルの端子で（ペアのチャネル 1-2・3-4 がもう一方のエッジを押さえる）、呼んでいる間タイマーを丸ごと、ていねいに借ります。そうでない端子や、タイマーが使用中のときは `pulseIn()` そのものです。`pulseIn()` はどの端子でも software のループのままで、タイマーのコードを入れません。捕捉は V003 で Flash 約 2 KB（タイマーの管理）かかるので、スケッチが選んで使います。

## 確かめたこと

2026-10-28、同じチップの GPIO・`tone()`・Servo で、測る端子そのものを動かして確かめました（配線なし）。系列は V003、V006、V103、V203、V205、V307、L103、X035、X315、H417 です。どの系列でも、端子を GPIO の出力にしたままで、タイマーはその端子のレベルを読みました。

- 周期と High の時間: 37 Hz〜5 kHz の `tone()` と Servo の 1500 µs。止まった線、`minHz` より遅い信号、チャネル 2 の端子。
- エンコーダ: x4 / x2 / x1、A と B を入れ替えたとき、±70000（16 ビットを越える）、`setCount`、`pauseCount`。
- 手放したあと、同じタイマーで `analogWrite()` が動くこと。

`CH32RVInputCapture`、`timerPulseIn()`、remap 先の端子、チャネル 3 / 4（2026-11-17）は、まだビルドだけです。tests/instrumented/signals（`test_edge_capture`、`test_timer_pulse_in`）がプローブの PWM と比べて確かめます。

## 例

- [PulseMeasure](examples/PulseMeasure/README.ja.md): 周波数・周期・パルス幅を測る。
- [RotaryEncoder](examples/RotaryEncoder/README.ja.md): ロータリーエンコーダを数える。
- [EdgeTimes](examples/EdgeTimes/README.ja.md): エッジごとの時刻と、その間の High と Low の時間を表示する。
