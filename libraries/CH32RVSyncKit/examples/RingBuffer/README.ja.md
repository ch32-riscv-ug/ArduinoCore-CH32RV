# RingBuffer

English: [README.md](README.md)

大きさの決まったバイト列です。UART から打った行を `Ring` に溜め、改行で 1 行として処理します。`write()` は入る分だけ取ります。

## 必要なものと配線

115200 bps・8N1 の UART モニタを `RX` / `TX` に（行末は LF か CRLF）。

## 操作と期待する結果

1. 1 行送ると `LINE bytes=n text=...` が返ります。
2. 65 文字以上の行を送ると、64 バイトを超えた分が捨てられ `OVERFLOW dropped=n` が出ます。

## 設定と制約

この `Ring` は `loop()` だけが使うので `LoopOnly`（割り込みを止めない）です。ISR と共有するなら既定の `IsrSafe` のままにします。

## 関連資料

- [サンプル共通の準備](../README.ja.md)
- [CH32RVSyncKit の API](../../README.ja.md)

[RingBuffer.ino](RingBuffer.ino)
