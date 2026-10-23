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

## API の使い方と改造のポイント

`line.write()` は保存できたバイト数を返します。メッセージを丸ごと保存する保証は無いので、複数 byte の入力を渡す場合も取得長を確認します。`line.read()` は取り出した長さだけ配列を埋めます。

この例の行境界は LF で、CR は読み捨てます。バイナリへ改造するときはこの加工を外します。受信を ISR からも行うように変えた場合は、`LoopOnly` の指定も見直します。

## 関連資料

- [サンプル共通の準備](../README.ja.md)
- [CH32RVSyncKit の API](../../README.ja.md)

[RingBuffer.ino](RingBuffer.ino)
