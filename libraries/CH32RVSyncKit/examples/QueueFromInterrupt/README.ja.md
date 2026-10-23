# QueueFromInterrupt

English: [README.md](README.md)

割り込みから `loop()` へ、`Queue` で値を渡します。ISR は押した時刻を入れてすぐ戻り、表示は `loop()` が行います。

## 必要なものと配線

`BUTTON_PIN`（既定 `PA1`）と GND の間に押しボタン。115200 bps・8N1 の UART モニタを `RX` / `TX` に。

## 操作と期待する結果

1. 起動すると `QueueFromInterrupt ready`。
2. ボタンを押すたびに `PRESS ms=... queued=...` が出ます。
3. 素早く 9 回以上押すと、`loop()` が 200 ms ごとにしか取り出さないので Queue（8 個）があふれ、`DROPPED n` が出ます。

## 設定と制約

ISR では `trySend()` が失敗しても待たずに数えて戻ります。満杯の Queue は新しい値を断り、古い値を残します（上書きしない）。押しボタンのチャタリングで 1 回の押下が複数になることがあります。

## API の使い方と改造のポイント

`trySend(value)` は値をコピーし、保存できれば `true` を返します。`tryReceive(value)` は一つ取り出せれば `true`、空なら待たずに `false` で戻ります。引数でポインタを渡す場合は、指す先の寿命と所有権を別に管理します。

この例の待ちは満杯を観察するためです。実際の用途では読み出し頻度を上げ、満杯時に捨てるか上位へエラーを返すかを決めます。補助の dropped カウンタも、正確な集計には取得・リセットを一まとまりで保護します。

## 関連資料

- [サンプル共通の準備](../README.ja.md)
- [CH32RVSyncKit の API](../../README.ja.md)

[QueueFromInterrupt.ino](QueueFromInterrupt.ino)
