# Serial

English: [README.md](README.md)

明示的に宣言した CDC ポートの送受信と、ホストによる line coding・DTR/RTS の変更を観察します。USB 通信とハードウェア UART ログを使い分ける例です。

## 必要なものと配線

PC の CDC 端末と、115200 bps・8N1 の UART モニタ。UART は `RX` / `TX` に対応する端子を使用します。

## 操作と期待する結果

1. UART モニタを開き、USB を PC に接続します。
2. CDC ポートを開くと UART に `CDC_LINE_CODING` / `CDC_LINE_STATE` が表示されます。
3. `abc` を送ると各文字に対し `echo: a`、`echo: b`、`echo: c` の行が返ります。
4. 開いている CDC ポートには約3秒ごとに `tick=... line_coding=... line_state=...` が届きます。

## 設定と制約

`onRx()` は受信数を通知し、実際の読み出しは `loop()` で行います。CR は捨て、LF は CRLF として返します。`UsbSerial` が USB CDC、`Serial` が UART です。CDC の baud は通知値で、USB の転送速度を設定する値ではありません。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[Serial.ino](Serial.ino)
