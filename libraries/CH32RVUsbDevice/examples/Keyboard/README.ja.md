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

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `setLayout(...)` | ASCII の文字を HID キーへ変換する表を選びます。PC のキーボード配列も合わせます。 |
| `write("...")` / `tapKey('\n')` | 文字列、または1文字の押下と解放を送ります。日本語の文章を UTF-8 から入力する API ではありません。 |
| `tapUsage(...)` | ASCII 変換を通さず HID usage を送ります。文字コードではなくキーの識別子を指定します。 |
| `onOutputReport()` / `ledState()` | PC が指示した Lock LED 状態を通知または参照で読みます。通知を使う場合は `deliverEvents()` が必要です。 |

配列を変えるのはデバイス側の文字変換であり、PC の設定変更や IME の切替ではありません。この例は途中で JIS の
変換表へ切り替えるので、記号が意図と違えば、その時点の PC 配列と IME の状態を確認します。

ボタンに置き換えるときは、押下のたびに `pressUsage()`、解放時に `releaseUsage()` または `releaseAll()` を送ります。
押した報告だけを送ると、PC は押し続けていると解釈します。送信に失敗した場合も解放の再試行を検討します。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[Keyboard.ino](Keyboard.ino)
