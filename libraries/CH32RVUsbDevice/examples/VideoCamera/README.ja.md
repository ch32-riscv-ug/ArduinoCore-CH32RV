# VideoCamera

English: [README.md](README.md)

カメラセンサなしで YUY2 カラーバーと動く白いブロックを生成する UVC デバイスです。

## 必要なものと配線

UVC を扱える PC アプリと UART モニタ。80×60 YUY2を選べるアプリを使用します。

## 操作と期待する結果

1. PC のカメラ一覧からこの USB Camera を選びます。
2. 80×60、YUY2、10 fpsで開きます。既定は bulk 転送です。
3. カラーバーだけでなく、画面下の白いブロックが動くことを確認します。
4. UART の `Host committed`、`Streaming started` / `Streaming stopped` を確認します。

## 設定と制約

1 frame は9600 bytesですが、`onFillPayload()` が payloadごとに生成するため frame全体の RAM buffer は使いません。`sendGeneratedFrame()` は loop 側で周期投入し、完了 callback は短く保ちます。

ISO に切り替えるには `device.begin()` の前に `Camera.setTransferType(CH32RVUsbDeviceVideoTransfer::Isochronous)` を呼びます。X035 の64 B/msではヘッダも必要なので、10 fps の維持は期待できません。frame rate を下げて転送量に合わせます。

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `setFormat()` / `setFrameSize()` / `setFrameRate()` | ホストに知らせる映像形式を USB 起動前に設定します。設定の成否は `bool` で返ります。 |
| `streaming()` / `frameInFlight()` | ホストが stream を開始したか、前の frame が転送中かを調べます。 |
| `sendGeneratedFrame(bytes)` | 指定長の frame の生成転送を開始します。実際のデータは `onFillPayload()` で要求されます。 |
| `onFillPayload(dst, length, offset)` | frame の `offset` byte目から `length` bytesを `dst` へ書きます。USB の処理中に呼ぶ provider です。 |
| `onCommit()` / `onFrameComplete()` | ホストの合意内容と frame 完了を `deliverEvents()` で通知します。 |

`offset` はピクセル番号ではなくバイト位置です。YUY2 の `Y0 U Y1 V` の途中から要求されても、
正しい値を書けるようにします。`onFillPayload()` は短く保ち、`delay()` や次の frame の送信開始を入れません。

生成に使う `drawingFrame` は転送中に変えず、一つの frame の中身を一定に保ちます。メモリ上の画像を使う
`sendFrame()` へ変更する場合も、転送完了まで画像バッファを保持します。周期を変更するときは広告する frame rate と
投入タイミングをそろえ、`onCommit()` が知らせるホストの frame interval は100 ns単位として解釈します。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[VideoCamera.ino](VideoCamera.ino)
