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

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `keyboard(device)` / `mouse(device)` | 同じデバイスへ両方の HID 機能を追加します。 |
| `keyboard.write("K")` | キーの押下・解放を送ります。入力先は PC でフォーカスされているアプリです。 |
| `mouse.move(40, 0)` | 右方向の相対移動を送ります。画面上の絶対位置ではありません。 |
| `mouse.click(...)` | ボタンの押下と解放を送ります。 |

HID interface は共通で、Report ID がキーボードとマウスを区別します。生 report を扱うアプリでは
interface の数だけから機能を数えず、report descriptor も確認します。

キーボード送信後に移動とクリックが続くため、入力先のフォーカスが変わることがあります。実際のボタン操作に
置き換えるときは、周期送信を外して各入力の変化へ結び付け、各 API の `bool` を確認します。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[KeyboardMouse.ino](KeyboardMouse.ino)
