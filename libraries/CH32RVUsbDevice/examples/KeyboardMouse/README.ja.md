# KeyboardMouse

English: [README.md](README.md)

一つの HID インターフェースにキーボードとマウスを合成する例です。Report ID で二つの機能を区別します。

## 必要なものと配線

PC のテキストエディタ、UART モニタ。文字入力・移動・クリックが自動実行されます。

## 操作と期待する結果

1. 約3秒ごとに `K` が入力され、ポインタが右へ40移動し、左クリックが送られます。
2. エディタと HID viewer で両方の機能を確認します。
3. UART の `KEY_FAILED` / `MOVE_FAILED` / `CLICK_FAILED` で失敗した機能を判別します。

## 設定と制約

別々の USB デバイスを二つ作る構成ではありません。同じ `device` に function を宣言し、`begin()` は1回です。複合 HID は Report ID を使うので、単独キーボードの boot report と同じ形とは限りません。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[KeyboardMouse.ino](KeyboardMouse.ino)
