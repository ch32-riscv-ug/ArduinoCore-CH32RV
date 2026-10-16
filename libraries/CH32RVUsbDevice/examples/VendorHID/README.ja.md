# VendorHID

English: [README.md](README.md)

vendor-defined HID の Input、Output、Feature report を観察します。bulk を使う USBVendor とは別の例です。

## 必要なものと配線

PC の HID アプリ（hidapi 等）と UART モニタ。report ID と usage page を指定できるものを使います。

## 操作と期待する結果

1. Report ID 6、usage page `0xff00` の HID を開きます。
2. 約1秒ごとに63-byte Input payload が届きます。先頭は `45 53 50`（`ESP`）、続いて counter の下位/上位 byte 等です。
3. Output または Feature report を送ると、UART に `VENDOR_OUTPUT` / `VENDOR_FEATURE` と hex が出ます。

## 設定と制約

この例は Output の自動エコーを実装していません。エコーが必要なら [CustomDevice](../CustomDevice/README.ja.md) を参照してください。PC の HID API が扱う report ID の1 byte と、63-byte payload を区別します。先頭の `ESP` は移植元由来のテストデータで、デバイスの型を示すものではありません。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[VendorHID.ino](VendorHID.ino)
