# Timer resource の契約

## 対象

`analogWrite()`、`tone()`、`Servo` と同梱 library は、`CH32RVTimer.h` の resource manager を通して TIM を使います。
SysTick は `millis()` / `micros()` / `delay()` の時間基盤専用で、TIM の貸出対象ではありません。

## 所有単位

- channel 0 (`CH32RV_TIMER_WHOLE`) は timer 全体の排他的所有
- channel 1 以降は capture/compare channel の所有
- 複数 channel は prescaler、reload、control が完全に一致するときだけ base counter を共有できる
- update interrupt は timer ごとに一つの owner だけが持つ

series ごとの timer 能力は生成された `CH32RV_TIMER_TABLE` が正本です。library は timer 番号や register address を
固定せず、`ch32TimerCapability*()` で問い合わせます。

## 取得方法

`ch32TimerTryAcquire()` は既存 owner と競合する場合に失敗し、状態を変えません。
`ch32TimerTakeover()` は互換な空き resource を先に探し、それが無い場合だけ既存 owner を停止して取得します。

takeover された owner には `quiesce` callback が呼ばれ、古い lease は generation の不一致により無効になります。
library は register を操作する前に有効な lease を保持し、終了時に `ch32TimerRelease()` します。

## Arduino API の規則

- `analogWrite()` は対象 pad の timer channel を取得する
- `tone()` は任意の pad を toggle するため timer 全体と update interrupt を取得する
- `Servo` は全 servo を一つの timer interrupt で順に駆動する
- `tone()` / `Servo` は指定された優先 timer、互換な空き timer、他の空き timer の順に探し、最後に優先 timer を takeover する

Arduino API は従来どおり last-caller-wins とし、resource を失った機能は pin を安全な停止状態へ戻します。
競合を許容できない library は `TryAcquire` を使い、取得失敗を利用者へ返します。

## 直接 register を使う場合

公開 API が扱わない timer 機能に `register_base` を使うことはできますが、有効な lease を保持したまま行います。
resource manager を迂回すると、`analogWrite()`、`tone()`、`Servo` が同じ timer を再設定するため動作は保証されません。
