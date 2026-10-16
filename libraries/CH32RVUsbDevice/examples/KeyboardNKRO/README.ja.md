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

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[KeyboardNKRO.ino](KeyboardNKRO.ino)
