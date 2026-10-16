# Keyboard

English: [README.md](README.md)

接続した USB キーボードの ASCII 入力を UART に表示し、Caps/Num Lock LED を更新します。

## 必要なものと配線

USB キーボード、Host の VBUS 配線、115200 bps UART モニタ。

## 操作と期待する結果

1. キーボードで英数字を入力し、UART で確認します。Enter は改行になります。
2. Caps Lock / Num Lock で対応 LED の切替を確認します。
3. JIS キーボードではコメント中の `setKeyboardLayout(...JA_JP)` を有効にし、再ビルドして記号を確認します。

## 設定と制約

この例は押下イベントと ASCII のみ表示します。release、修飾キー単独、日本語 IME の変換は画面に出ないことがあります。`setKeyboardLayout()` は Host ライブラリ側の文字変換で、キーボード自体へ配列を書き込む操作ではありません。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbHost の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[Keyboard.ino](Keyboard.ino)
