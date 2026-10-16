# Keyboard

English: [README.md](README.md)

USB キーボードとして、文字列・JIS 記号・raw HID usage を送信し、Lock LED の Output report を受信します。

## 必要なものと配線

PC のテキストエディタと UART モニタ。接続後は約3秒ごとに自動入力が始まるため、入力先をエディタにしておきます。

## 操作と期待する結果

1. UART に `USB keyboard ready` が出ることを確認します。
2. エディタで `Hi from CH32RVUsbDevice` と後続の記号入力を確認します。
3. Caps Lock 等を切り替えると UART に `LEDS num=... caps=...` が出ます。
4. スケッチは US→JIS→US と変換表を切り替え、`LANG1` usage も送ります。

## 設定と制約

`setLayout()` は文字から HID usage への変換を選びます。PC のキーボード配列を変更する API ではないので、記号を照合するときは PC 側の配列も合わせます。`onOutputReport()` は1枠で、現在値は `ledState()` からも取得できます。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[Keyboard.ino](Keyboard.ino)
