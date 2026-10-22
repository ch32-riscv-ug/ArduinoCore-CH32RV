# CriticalSection

English: [README.md](README.md)

ISR が書く複数の変数を、矛盾のない 1 組として読みます。`CriticalSection` のスコープの間だけこのコアの割り込みを止めます。

## 必要なものと配線

`BUTTON_PIN`（既定 `PA1`）と GND の間に押しボタン。115200 bps・8N1 の UART モニタを `RX` / `TX` に。

## 操作と期待する結果

1. 1 秒ごとに `PRESSES count=... last_ms=... sum_us_low=...` が出ます。
2. 押すたびに `count` が増え、`last_ms` がその時刻になります。

## 設定と制約

止めている間は USB・USB PD・UART などすべての割り込みが待たされるので、中ではコピーだけを行い、表示は外で行います。`CriticalSection` は入れ子にでき、元の状態（すでに止まっていれば止まったまま）に戻します。CH32V003 には原子命令が無いので、2 ワード以上の値を ISR と共有するにはこの方法を使います。ISR の中ではこのコアの割り込みはすでに止まっているので、ISR 側に保護は要りません。

## 関連資料

- [サンプル共通の準備](../README.ja.md)
- [CH32RVSyncKit の API](../../README.ja.md)

[CriticalSection.ino](CriticalSection.ino)
