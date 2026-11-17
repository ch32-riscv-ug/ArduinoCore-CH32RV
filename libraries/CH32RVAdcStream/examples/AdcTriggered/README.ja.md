# AdcTriggered

English: [README.md](README.md)

アナログの端子を 1 本、1 秒に 1000 回のタイマーの刻みごとに 1 回変換し、1 秒ごとに届いた数を表示します。次に、この石で ADC のトリガになれる最初の端子の立ち上がりごとに 1 回の変換に切り替え、変換を 1 回ずつ表示します。

## 必要なものと配線

測りたい電圧を `INPUT_PIN`（既定 `A0`、0 V〜電源の電圧）に入れます。縁の部分には、スケッチが表示するトリガの端子（`ADC_EDGE trigger pin=...`。どの端子がなれるかは石による。[ライブラリの README](../../README.ja.md)）にスイッチか信号を。スケッチがプルダウンします。115200 bps・8N1 の UART モニタを `RX` / `TX` に。

## 操作と期待する結果

1. 起動すると `ADC_TIMER rate=1000 timer=TIM3 step=7`（コアが取ったタイマーとサンプル時間の段。タイマーは系列で違う）。
2. 1 秒ごとに `ADC_TIMER conversions=... (+1000) last=...`。タイマーのクロックでちょうど毎秒 1000 回です。
3. 5 秒後、トリガの端子がある石なら `ADC_EDGE trigger pin=...`、その端子の立ち上がりごとに `ADC_EDGE <n> value=...`（縁のときの `INPUT_PIN` の値）。

## 設定と制約

- `TIMER_RATE` はタイマーの届く値なら何でもよく、自走（`begin()`）の一番遅いレートよりずっと遅くも、1 Hz 未満にもできます。
- タイマーは `analogWrite()`・`tone()`・`timerBegin()` などとコアのタイマーの管理で分け合います。その系列のトリガのタイマーに空きが無ければ `beginTimer()` は false です。
- 値は変換器のままで、`analogRead()` の換算はかけていません。

## 関連資料

- [CH32RVAdcStream の API](../../README.ja.md)

[AdcTriggered.ino](AdcTriggered.ino)
