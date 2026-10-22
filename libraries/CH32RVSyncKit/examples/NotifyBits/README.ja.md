# NotifyBits

English: [README.md](README.md)

種類の違うイベントを 1 つにまとめます。ISR ごとにビットを立て、`loop()` は「どれか」か「全部」を `tryWaitBits()` で調べます。

## 必要なものと配線

`BUTTON_A`（既定 `PA1`）・`BUTTON_B`（既定 `PA2`）と GND の間に押しボタン。115200 bps・8N1 の UART モニタを `RX` / `TX` に。

## 操作と期待する結果

1. 500 ms ごとに `WAITING A B`（まだ押されていないもの）が出ます。
2. A と B を両方押すと（順番は問わない）`BOTH` が出て、また `WAITING A B` に戻ります。

## 設定と制約

`waitAll=true` は全ビットがそろったときだけ取り出します。`clearOnExit=false` は見るだけで消しません。同じビットを取り出す前に 2 回立てても 1 回です。

## 関連資料

- [サンプル共通の準備](../README.ja.md)
- [CH32RVSyncKit の API](../../README.ja.md)

[NotifyBits.ino](NotifyBits.ino)
