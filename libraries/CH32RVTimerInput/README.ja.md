# CH32RVTimerInput

[English](README.md)

タイマーを入力に使います。2 つのクラスがあります。

- **`CH32RVPulseCapture`:** 信号の周期と High の時間（パルス幅）を測ります。
- **`CH32RVEncoder`:** 直交のロータリーエンコーダを数えます。

どちらも、数えるのはタイマーです。スケッチは、ほしいときに結果を読むだけです。割り込みからスケッチの関数を呼ぶことはありません。読みに行かなくても、ハードは数え続けます。

```cpp
#include <CH32RVTimerInput.h>

CH32RVPulseCapture pulse;
CH32RVEncoder knob;

void setup() {
  Serial.begin(115200);
  pulse.begin(PA6);                    // タイマーのチャネル 1 か 2 の端子
  knob.attachFullQuad(PB6, PB7);       // 1 つのタイマーのチャネル 1 と 2
}

void loop() {
  CH32RVPulseCapture::Pulse p;
  if (pulse.read(p)) {                 // 前の read() から後の、最新の 1 周期
    Serial.println(p.frequencyHz());
  }
  Serial.println((long)knob.getCount());
}
```

## 端子とタイマー

- 使える端子は、タイマーのチャネル 1 か 2 です。そのボードの既定の経路で、`analogWrite()` がそのタイマーで PWM を出す端子と同じです（variant の `CH32RV_PWM_PIN_TO_TIMER`）。
- それ以外の端子では、`begin()` / `attach*()` が false を返します。エンコーダの A と B は、同じタイマーのチャネル 1 と 2 でなければなりません。
- タイマーは丸ごと借ります（[Timer resource の契約](../../docs/timer-design.ja.md)）。`analogWrite()`、`tone()`、Servo が先に使っていれば、false を返します。
- あとから `analogWrite()` などが同じタイマーを取ると、こちらは止まります。`read()` は false、`getCount()` は最後の値、`isAttached()` は false になります。
- タイマーのクロックは、どの系列でも F_CPU と同じです。ストップとスタンバイの間はタイマーも止まり、その間のエッジは数えません。

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

## 確かめたこと

2026-10-28、同じチップの GPIO・`tone()`・Servo で、測る端子そのものを動かして確かめました（配線なし）。系列は V003、V006、V103、V203、V205、V307、L103、X035、X315、H417 です。どの系列でも、端子を GPIO の出力にしたままで、タイマーはその端子のレベルを読みました。

- 周期と High の時間: 37 Hz〜5 kHz の `tone()` と Servo の 1500 µs。止まった線、`minHz` より遅い信号、チャネル 2 の端子。
- エンコーダ: x4 / x2 / x1、A と B を入れ替えたとき、±70000（16 ビットを越える）、`setCount`、`pauseCount`。
- 手放したあと、同じタイマーで `analogWrite()` が動くこと。

## 例

- [PulseMeasure](examples/PulseMeasure/README.ja.md): 周波数・周期・パルス幅を測る。
- [RotaryEncoder](examples/RotaryEncoder/README.ja.md): ロータリーエンコーダを数える。
