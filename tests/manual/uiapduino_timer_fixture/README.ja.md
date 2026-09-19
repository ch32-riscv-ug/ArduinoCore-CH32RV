# UIAPduino V1.4 timer HIL

16 KiBのV003で全周辺fixtureとタイマー競合試験を同じimageへ入れると容量を超えるため、
タイマーだけを独立した第2imageで試験します。ESP32 E132 fixtureのedge計測を使用します。

確認項目:

- SysTick由来の`millis()`と`delay()`
- 公開`CH32Timer` APIによるTIM2全体lease
- polite acquireからtakeoverした際のquiesceと旧lease無効化
- 1 ms update interrupt、detach、release後の状態
- `analogWrite(PC3, 64)`の約1 kHz、25% duty波形
- `tone(PC0, 1000, 400)`の約1 kHz、50% duty波形

SWIOとRESETは試験中に使用しません。書込み時のboot entryだけ、既存のE132 `B`操作を使います。

```sh
uv run tests/manual/uiapduino_timer_fixture/uiapduino_timer_fixture.py
```

## 2026-09-19 実機結果

専用FQBNで9,140 byte、RAM 480 byte。製品HIDから書き込み、次を確認しました。

- `delay(125)`に対する`millis()`差分: 125 ms
- TIM2 whole lease: start成功
- owner Aからowner Bへのtakeover: 旧lease無効、quiesce 1回
- 1 ms update interrupt: 30 ms待機中に29回
- detach/release後: TIM2 `configured=false`
- PWM PC3/TIM1_CH3: 250.001 ms中、rise 252、fall 251、HIGH 62.554 ms、duty 25.0%
- tone PC0/TIM2: 250.000 ms中、rise/fall各249、HIGH 125.052 ms、duty 50.0%

最終結果は`PASS: timer HIL`です。
