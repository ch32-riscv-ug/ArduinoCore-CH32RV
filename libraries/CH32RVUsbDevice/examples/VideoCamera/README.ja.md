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

ISO に切り替えるには `build_opt.h` へ `-DCFG_TUD_VIDEO_STREAMING_BULK=0` を追加します。X035 の64 B/msではヘッダも必要なので、10 fps の維持は期待できません。frame rate を下げて転送量に合わせます。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[VideoCamera.ino](VideoCamera.ino)
