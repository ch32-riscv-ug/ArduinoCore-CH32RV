# TryLock

English: [README.md](README.md)

`loop()` が何回かに分けて埋めるバッファを、その間 ISR に触らせません。ISR は待てないので、ロックを試して取られていれば飛ばします。

## 必要なものと配線

`BUTTON_PIN`（既定 `PA1`）と GND の間に押しボタン。115200 bps・8N1 の UART モニタを `RX` / `TX` に。

## 操作と期待する結果

1. 400 ms ごとに `DONE isr_writes=... isr_skips=...` が出ます。
2. 押したときにバッファが埋めている途中なら `isr_skips`、そうでなければ `isr_writes` が増えます。

## 設定と制約

ISR が戻るまで `loop()` は動けないので、ISR が `loop()` のロック解放を待つと永久に止まります。ロックを `loop()` をまたいで持つのは、この例のように意図があるときだけにします。

## 関連資料

- [サンプル共通の準備](../README.ja.md)
- [CH32RVSyncKit の API](../../README.ja.md)

[TryLock.ino](TryLock.ino)
