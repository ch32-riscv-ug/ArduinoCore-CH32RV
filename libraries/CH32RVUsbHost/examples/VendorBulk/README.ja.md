# VendorBulk

English: [README.md](README.md)

vendor-specific bulk interface を開き、UART 入力を OUT へ送り、IN の受信を hex 表示します。

## 必要なものと配線

既知の bulk IN/OUT protocol を持つ USB Device。組み合わせ例は別ボードの CH32RVUsbDevice USBVendor。Host への VBUS と UART モニタが必要です。

## 操作と期待する結果

1. 接続時に `vendorOpen: ok` が出ることを確認します。
2. UART から文字を送ります。
3. USBVendor peer なら `echo: ...` が `in: ...` の hex として返り、周期 status も受信します。

## 設定と制約

既定は見つかった vendor interface を選びます。VID/PID だけから相手の protocol が分かるわけではないため、任意の vendor 機器へ文字列を送る用途ではありません。ログには payload の文字列ではなく hex を表示します。長いデータは最大64 bytes単位に分割され、メッセージ境界を保存しません。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbHost の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[VendorBulk.ino](VendorBulk.ino)
