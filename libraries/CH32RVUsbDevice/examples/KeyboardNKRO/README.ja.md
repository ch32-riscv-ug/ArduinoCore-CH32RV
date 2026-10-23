# KeyboardNKRO

English: [README.md](README.md)

NKRO の bitmap report で10キーと Left Shift を同時送信します。boot protocol では6キー形式に切り替わることも観察できます。

## 必要なものと配線

PC の HID report viewer またはキー状態表示アプリ、UART モニタ。自動入力されるので、テスト用の入力先で使用します。

## 操作と期待する結果

1. `keyboard.enableNkro()` が `device.begin()` より前に呼ばれることを確認します。
2. 約4秒ごとに A/S/D/F/G/H/J/K/L/セミコロンと Left Shift が約200 ms 押されます。
3. `releaseAll()` 後に UART で `A still down? no` を確認します。
4. 文字入力だけでは同時性を判断できないため、report またはキー状態で確認します。

## 設定と制約

`sendReport()` は held state 全体を1 report にします。`pressUsage()` の連続呼出しとは送信回数が違います。boot/report の選択はホスト側に依存し、boot protocol 中は NKRO の全キーを表現できません。

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `enableNkro()` | `device.begin()` より前に呼び、複数キーを bitmap で表す report を宣言します。 |
| `CH32RVUsbDeviceNkroKeyboardReport::press(usage)` | 送信前の report にキーを追加します。修飾キーも usage で指定できます。 |
| `keyboard.sendReport(report)` | 組み立てたキー集合を一つの report として送ります。失敗は `bool` の戻り値で確認します。 |
| `releaseAll()` / `heldState()` | 全キーの解放を送り、ライブラリが保持する送信済みのキー状態を確認します。 |
| `protocol()` | ホストが選んだ boot/report protocol を確認します。NKRO の有効設定とは別です。 |

`pressUsage()` を繰り返すと、そのたびに状態が送られます。同時押しを一度に見せたいときは、この例のように report を
組み立ててから送ります。boot protocol では同じ数のキーを表せないため、設定だけで動作を判断せずホスト側も確認します。

`heldState()` は物理スイッチの状態を読むものではありません。実キーボードへ改造する場合はスイッチの走査と
debounce を別途行い、変化があったときに送信します。この例は送信成否をすべて表示する作りではありません。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[KeyboardNKRO.ino](KeyboardNKRO.ino)
