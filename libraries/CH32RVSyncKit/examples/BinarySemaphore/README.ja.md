# BinarySemaphore

English: [README.md](README.md)

「起きた」を 1 回だけ伝えます。ISR が `give()`、`loop()` が `tryTake()`。取り出す前の 2 回目の `give()` は断られます。

## 必要なものと配線

`BUTTON_PIN`（既定 `PA1`）と GND の間に押しボタン。115200 bps・8N1 の UART モニタを `RX` / `TX` に。

## 操作と期待する結果

1. 押すと 1 秒以内に `PRESSED`。
2. 1 秒の間に 2 回以上押すと `MISSED n` が出ます（`loop()` が遅れたことが分かる）。

## 設定と制約

回数が要るなら `Notify` のカウンタを使います。

## API の使い方と改造のポイント

`give()` は未処理の合図を一つ保持し、既にあれば `false` を返します。`tryTake()` は合図がある場合だけ取り出して `true` を返し、無ければ待たずに戻ります。回数そのものを保存したい場合は NotifyCounter を選びます。

補助の `missed` カウンタまで正確に取得・リセットしたい場合は、短い `CriticalSection` 内で値のコピーと0への更新をまとめ、表示は外で行います。`volatile` だけでは「読み取ってから消す」の間の割り込みを防げません。

## 関連資料

- [サンプル共通の準備](../README.ja.md)
- [CH32RVSyncKit の API](../../README.ja.md)

[BinarySemaphore.ino](BinarySemaphore.ino)
