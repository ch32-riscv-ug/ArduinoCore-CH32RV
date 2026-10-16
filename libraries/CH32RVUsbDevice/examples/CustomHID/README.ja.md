# CustomHID

English: [README.md](README.md)

独自の report descriptor を渡して、8-byte Input report だけを持つ HID を作ります。

## 必要なものと配線

HID report を生で読める PC アプリと UART モニタ。

## 操作と期待する結果

1. usage page `0xff00`、usage 1 の HID を開きます。
2. 約1秒ごとに `45 53 50 counter timeLo timeHi aa 55` の8 bytes が届きます。
3. UART の `CUSTOM_HID_TX counter=...` と比較します。

## 設定と制約

descriptor に Report ID はなく、8-byte Input だけを宣言しています。Output/Feature を送りたい場合は descriptor と callback の両方を拡張します。constructor の report 長、Report Count、送信する配列長を一致させます。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[CustomHID.ino](CustomHID.ino)
