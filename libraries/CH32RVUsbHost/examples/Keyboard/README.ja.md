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

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `usb.onKeyboard(callback)` | キーイベントを `deliverEvents()` で受け取ります。 |
| `e.pressed` / `e.keycode` / `e.ascii` | 押下か解放か、HID usage、変換後の文字を区別します。文字にできないキーでは `ascii` が0の場合があります。 |
| `setKeyboardLayout(...)` | usage から文字への変換表を選びます。JIS を使うならコメント中の設定を有効にします。 |
| `setKeyboardLeds(num, caps, scroll, e.address)` | イベントを送ってきたキーボードへ Lock LED 状態を通知します。 |

この例は押下イベントだけを表示します。キーが押され続けている間のリピートを自分の用途で必要とするなら、
押下・解放状態と時刻を保持して実装します。`ascii` の変換は日本語 IME や Unicode の入力処理ではありません。

Caps/Num の状態変数はスケッチ全体で一組です。複数キーボードを独立に扱う場合は address ごとに管理し、
抜去後の状態も更新します。PC への USB キーボード中継機能はこの例には含まれていません。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbHost の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[Keyboard.ino](Keyboard.ino)
