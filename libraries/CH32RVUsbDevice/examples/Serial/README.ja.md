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

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `CH32RVUsbDeviceCdcSerial UsbSerial(device)` | CDC を指定したデバイスへ追加します。`Serial` は UART ログ、`UsbSerial` は USB の相手との通信です。 |
| `device.begin(config)` | function と設定がそろってから起動します。`false` の場合は UART へ `device.lastErrorName()` を表示します。 |
| `onLineCoding()` / `onLineState()` | ホストが指定した通信条件と DTR/RTS を観察します。通知は `deliverEvents()` で受け取ります。 |
| `onRx()` | 受信があることを知らせます。登録するだけで読み出されるわけではなく、この例では実際の `read()` を `loop()` に置いています。 |
| `flush()` | 送信待ちのデータの処理を進めます。受信バッファを消す API ではありません。 |

`begin()` の成功、`device.mounted()` による列挙完了、`UsbSerial.connected()` による DTR は別の状態です。
端末を開いても定期表示が来なければ、まず DTR と開いたポートを確かめます。ホストの baud 指定を受けても
ハードウェア UART の速度は変わらず、ブリッジを作る場合はスケッチで別途反映します。

改行処理もサンプルの一部です。CR は読み捨て、LF は CRLF にし、通常の文字は1文字ずつ `echo: ` を付けます。
バイナリ中継へ改造するときはこの文字処理を外し、送信長の戻り値を扱います。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[Serial.ino](Serial.ino)
