# Timer resource の契約

## 対象

`analogWrite()`、`tone()`、`Servo`、利用者向けのタイマー API（`timerBegin()` など、`CH32RVHwTimer.h`）と同梱 library
（CH32RVTimerInput、CH32RVTimerOutput、Ticker ほか）は、`CH32RVTimer.h` の resource manager を通して TIM を使います。
SysTick は `millis()` / `micros()` / `delay()` の時間基盤専用で、TIM の貸出対象ではありません。

## 所有単位

- channel 0 (`CH32RV_TIMER_WHOLE`) は timer 全体の排他的所有
- channel 1 以降は capture/compare channel の所有
- 複数 channel は prescaler、reload、control が完全に一致するときだけ base counter を共有できる
- update interrupt は timer ごとに一つの owner だけが持つ
- capture/compare の割込み（`ch32rv_timer_attach_channel_interrupt()`）も timer ごとに一つの owner。timer 全体の lease が要る。
  高機能 timer は CC のベクタ（`CH32RV_TIMER_CC_TABLE`）、ほかは update と同じベクタで、channel の callback が先に呼ばれる

series ごとの timer 能力は生成された `CH32RV_TIMER_TABLE` が正本です。library は timer 番号や register address を
固定せず、`ch32rv_timer_capability*()` で問い合わせます。

## 端子と経路

timer の channel・相補出力（CHxN）・ブレーキ入力（BKIN）が届く端子は、variant の `CH32RV_TIMER_PADS`（device-data の全経路）が正本です。
`ch32rv_timer_pad_find(pin, roles, n, &pad)` が端子の n 番目の届き方（timer、channel、役割、経路）を返し、`ch32rv_timer_pad_connect()`
が経路を選びます: AFIO の欄が timer ごとに 1 つの系列では timer の remap の値（その timer の端子が全部移る）、V205・X305/X315・H41x
では端子ごとの AF 番号です。系列の違いはこの 2 つと `ch32rv_timer_pads_together()`（2 つの端子を同時につなげるか）がコアで吸収し、
library は系列を `#if` で分けません。1 つの端子の届き方は、その端子の既定の PWM（`CH32RV_PWM_PIN_TO_*`）が先で、remap の無い経路、
channel、小さい timer の順です。

## 取得方法

`ch32rv_timer_try_acquire()` は既存 owner と競合する場合に失敗し、状態を変えません。
`ch32rv_timer_takeover()` は互換な空き resource を先に探し、それが無い場合だけ既存 owner を停止して取得します。

takeover された owner には `quiesce` callback が呼ばれ、古い lease は generation の不一致により無効になります。
library は register を操作する前に有効な lease を保持し、終了時に `ch32rv_timer_release()` します。

manager は owner（`CH32RVTimerOwner`）を写さず、ポインタで持ちます。owner のオブジェクトは lease の間ずっと有効でなければならず、
static なオブジェクトか library のインスタンスのメンバーに置きます（関数の中の変数は不可）。写しをやめたことで、V003 の 2 つの
timer の状態は 216 B から 112 B になりました。

## 大きさ

update interrupt（`ch32rv_timer_attach_update_interrupt()`、振り分け、TIM のベクタ）は `ch32rv_timer_irq.c` にあり、attach する
スケッチだけがリンクします。`analogWrite()` だけのスケッチには入りません。V003 の `analogWrite()` 1 本のスケッチは Flash 2756 B /
RAM 632 B（空のスケッチは 728 B / 520 B）です。capture/compare の割込み（`ch32rv_timer_cc.c`）、端子の表（`ch32rv_timer_pad.c`）、
利用者向けのタイマー（`ch32rv_hw_timer.c`）もそれぞれ使うスケッチだけがリンクします。

使わない機能は大きさに入れません（2026-11-17）。`analogWrite()` は Arduino.h の関数形マクロが、コンパイル時に決まる端子を
振り分けます（`__builtin_constant_p` と variant の `CH32RV_PWM_PIN_TO_*`）。

- 既定の PWM のある定数の端子は `ch32rv_pwm_default()`（wiring_pwm.c）。端子の表も経路の切り替えも入らない。V003 の Fade は
  2552 B / 220 B（以前 2560 B / 220 B）
- remap 先・CHxN の端子と、実行時に決まる端子は `analogWrite()`（wiring_pwm_any.c）。表、経路の選択、追い出した端子の停止が入る。
  wiring_pwm.c の弱い関数（`ch32rv_pwm_make_room`、`ch32rv_pwm_slot_of`、`ch32rv_timer_route_set`）をこちらとch32rv_timer_pad.c
  が置き換える
- `pulseIn()` はどの端子でも software のループのまま。timer の捕捉は CH32RVTimerInput の `timerPulseIn()` を選んで呼んだ
  スケッチだけに入る（V003 で約 2 KB、timer の管理一式。ユーザー 2026-11-17）

`&analogWrite` は関数のままです。

## Arduino API の規則

- `analogWrite()` は対象 pad の timer channel を取得する。pad の届き方のうち、動いている PWM を動かさずに済む最初のものを使い、
  無ければ最初のものを取る。channel や経路が別の pad へ移ると、前の pad の PWM は止まり Low になる。tone/Servo の TIM4/TIM5 は使わない
  （`CH32RV_PWM_TIMER_MASK`）
- `timerPulseIn()`（CH32RVTimerInput）は pad が timer の channel で、その timer が空いていれば丸ごと politely 借りて捕捉し、返す。
  借りられなければ `pulseIn()`（software）
- `timerBegin()` は channel のある空き timer を番号の大きい方から politely 借りる。Ticker は全 Ticker でそのうち 1 つを使う
- `tone()` は任意の pad を toggle するため timer 全体と update interrupt を取得する
- `Servo` は全 servo を一つの timer interrupt で順に駆動する
- `tone()` / `Servo` は指定された優先 timer、互換な空き timer、他の空き timer の順に探し、最後に優先 timer を takeover する

Arduino API は従来どおり last-caller-wins とし、resource を失った機能は pin を安全な停止状態へ戻します。
競合を許容できない library は `TryAcquire` を使い、取得失敗を利用者へ返します。

## 直接 register を使う場合

公開 API が扱わない timer 機能に `register_base` を使うことはできますが、有効な lease を保持したまま行います。
resource manager を迂回すると、`analogWrite()`、`tone()`、`Servo` が同じ timer を再設定するため動作は保証されません。
