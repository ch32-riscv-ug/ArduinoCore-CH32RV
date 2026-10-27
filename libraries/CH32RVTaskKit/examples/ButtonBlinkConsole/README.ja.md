# ButtonBlinkConsole

English: [README.md](README.md)

ボタン、LED の点滅、UART のコマンドを 1 つのスケッチにまとめる例です。[スケッチの組み立て方](../../../../docs/sketch-guide.ja.md)
で順に見てきた書き方を、全部使います。仕事は 4 つで、それぞれ違う起動条件で動きます。

| 仕事 | 起動条件 | すること |
|---|---|---|
| `blink` | `Period`（周期） | LED を切り替える |
| `button` | ISR からの `post()` | ISR が `Queue` に入れた押した時刻を処理する |
| `console` | 条件の関数（UART にバイトがある） | 1 行ずつ読み、コマンドを実行する |
| `resume` | `Deadline`（期限） | 一時停止を終える |

どの仕事も待たずに戻り、`loop()` は `runOnce()` の 1 行だけです。ISR は記録して戻るだけで、割り込みを止めるのは、
ISR が書く 2 つの数を読む 1 か所だけです。

## 必要なものと配線

- `LED_PIN`（既定 `PA2`）に、抵抗を付けた LED。
- `BUTTON_PIN`（既定 `PA1`）と GND の間に押しボタン。内部のプルアップを使います。
- 115200 bps・8N1 の UART モニタを `RX` / `TX` に。改行（LF）を送る設定にします。

端子は `.ino` の定数で変えます。どの系列でも動きます（CH32V003 を含む）。

## 操作と期待する結果

1. 起動すると `ButtonBlinkConsole ready: rate <ms>, pause, stats` が出て、LED が 0.5 秒ごとに切り替わります。
2. ボタンを押すと `PRESS ms=... paused` が出て、点滅が止まります。もう一度押すと `... running` で再開します。
   チャタリングで続けて来た割り込みは、50 ms 以内なら 1 回として扱います。
3. `rate 100` を送ると `RATE ms=100` が出て、点滅が速くなります（20〜5000 ms）。
4. `pause` を送ると `PAUSE 5 s` が出て点滅が止まり、5 秒後に `RESUME` で再開します。
5. `stats` を送ると、ISR の回数と最後の時刻（`ISR count=... last_ms=...`）、仕事ごとの回数と最長時間
   （`BLINK runs=... max_us=...` など）、巡回の数（`ROUNDS total=... empty=...`）が出ます。
   `ISR count` がボタンの回数より多いのはチャタリングの分です。

## 設定と制約

- 押した時刻はデータとして `Queue` に、「処理して」という合図は `post()` に、と分けています。`post()` は回数を
  数えないので、`button` の 1 回の実行で、溜まった時刻を全部取り出します。
- `Queue` が満杯（8 回分）のときの押下は捨てられます。ISR の中では待ちません。
- `isrCount` と `isrLastMs` は ISR が一緒に書くので、`stats` では `CriticalSection` の中で 2 つをコピーしてから表示します。
  中で表示しないのが決まりです。
- 仕事が自分の時間を使い切ると、ほかの仕事が遅れます。`stats` の `max_us` で、どの仕事が長いかを確かめられます。
  時間には途中で動いた割り込みの分も入ります。

## API の使い方と改造のポイント

`wakeEvery()`、`wakeWhen()`、`wakeOn()`、`post()` は仕事を動かす条件を定め、`runOnce()` が実行します。期限や周期のオブジェクト自体が、別のタスクを作るわけではありません。

コマンドを追加するときは、行を少しずつ蓄積する構造を保ち、コマンド内で完了を待ち続けないようにします。長い操作は開始・進行・終了の状態を分けます。ISR と共有する複数値は既存の `CriticalSection` と同様に一組でコピーし、表示は外で行います。

## 関連資料

- [スケッチの組み立て方](../../../../docs/sketch-guide.ja.md)
- [サンプル共通の準備](../README.ja.md)
- [CH32RVTaskKit の API](../../README.ja.md)
- [CH32RVSyncKit の API](../../../CH32RVSyncKit/README.ja.md)

[ButtonBlinkConsole.ino](ButtonBlinkConsole.ino)
