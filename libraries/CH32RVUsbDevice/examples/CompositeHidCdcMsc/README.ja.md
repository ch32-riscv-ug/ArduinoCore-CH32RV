# CompositeHidCdcMsc

English: [README.md](README.md)

キーボード、CDC、10 KiB FAT RAM ディスクを一つの USB デバイスにまとめます。CDC からキーボード入力を指示できます。

## 必要なものと配線

PC の CDC 端末、テキストエディタ、ファイルブラウザ、UART モニタ。RAM ディスクと USB buffer が収まる型番を使います。

## 操作と期待する結果

1. CDC、HID、`CH32RV` ボリュームが同時に現れることを確認します。
2. CDC に普通の文字列と改行を送ると `echo: ...` が返ります。
3. `type Hello` と改行を送ると、現在フォーカスのある画面にキーボードとして `Hello` と改行が入力されます。
4. CDC には約5秒ごとに `tick=...` が届きます。

## 設定と制約

ホストの入力先を先にテスト用エディタへ切り替えます。CDC のコマンドバッファは128 bytesで、長い行は先頭127文字までです。RAM ディスクは reset で消えます。endpoint は HID 1、CDC 2、MSC 1 の計4番号を使い、ESP32 の IN endpoint 上限の説明は CH32 に当てはめません。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[CompositeHidCdcMsc.ino](CompositeHidCdcMsc.ino)
