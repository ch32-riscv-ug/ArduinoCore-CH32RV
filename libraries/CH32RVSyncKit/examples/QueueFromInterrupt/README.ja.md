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

## 関連資料

- [サンプル共通の準備](../README.ja.md)
- [CH32RVSyncKit の API](../../README.ja.md)

[QueueFromInterrupt.ino](QueueFromInterrupt.ino)
