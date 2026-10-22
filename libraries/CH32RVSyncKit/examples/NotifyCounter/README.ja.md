# NotifyCounter

English: [README.md](README.md)

データ無しで回数だけを伝えます。ISR が `notify()`、`loop()` が `tryTakeAll()` で取り出します。

## 必要なものと配線

`BUTTON_PIN`（既定 `PA1`）と GND の間に押しボタン。115200 bps・8N1 の UART モニタを `RX` / `TX` に。

## 操作と期待する結果

1. 押した回数が 500 ms ごとに `PRESSES since_last=n` で出ます。

## 設定と制約

1 件ずつ処理するなら `tryTake()` を使います。カウンタは上限で止まり、折り返しません。

## 関連資料

- [サンプル共通の準備](../README.ja.md)
- [CH32RVSyncKit の API](../../README.ja.md)

[NotifyCounter.ino](NotifyCounter.ino)
