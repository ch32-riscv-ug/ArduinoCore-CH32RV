# WorkFromInterrupt

English: [README.md](README.md)

割り込みは仕事をせず、仕事を頼みます。ISR は押した時刻を `Queue` に入れて `post()` し、`loop()` の `runOnce()` が処理します。

## 必要なものと配線

`BUTTON_PIN`（既定 `PA1`）と GND の間に押しボタン。115200 bps・8N1 の UART モニタを `RX` / `TX` に。

## 操作と期待する結果

1. 押すと `PRESS ms=...` と `RUN handled=1`。
2. 300 ms の間に何度か押すと、1 回の `RUN handled=n` でまとめて処理されます（`post()` は何度呼んでも 1 回の実行）。

## 設定と制約

データは `Queue` に、合図は `post()` に、と分けます。`post()` は回数を数えないので、回数が要るものはデータ側に残します。

## API の使い方と改造のポイント

`handler.post()` は実行予約で、ISR のその場で関数を呼ぶ API ではありません。`runOnce()` が予約を見て処理を実行します。発生ごとのデータは `Queue::trySend()` / `tryReceive()` で別に渡します。

Queue が満杯でも post 自体から失われたデータは復元できません。必要なら `trySend()` の失敗を数え、処理関数では一度の起動で残っているデータを読み出します。

## 関連資料

- [サンプル共通の準備](../README.ja.md)
- [CH32RVTaskKit の API](../../README.ja.md)

[WorkFromInterrupt.ino](WorkFromInterrupt.ino)
