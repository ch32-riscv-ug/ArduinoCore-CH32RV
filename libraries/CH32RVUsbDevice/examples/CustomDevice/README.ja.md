# CustomDevice

English: [README.md](README.md)

CDC、DFU runtime、Vendor HID を組み合わせ、製品名や VID/PID を設定する例です。Vendor HID の Output report は Input report として返します。

## 必要なものと配線

PC の CDC 端末、HID report を送受信するアプリ、115200 bps の UART モニタ。

## 操作と期待する結果

1. UART に `USB started` が出て、PC に CDC と HID が現れることを確認します。
2. CDC に送ったデータは同じバイト列で返ります。
3. Vendor HID に Report ID 6 の Output report を送ると Input report として返ります。
4. UART の `host configured the device` / `host gone` で列挙状態を観察します。

## 設定と制約

Vendor HID の payload は最大63 bytes、Report ID は6です。HID アプリの API が report ID をバッファ先頭へ含める形式か確認します。`config.serialNumber=nullptr` はチップ固有値を使います。`maxPowerMilliamps=100` は descriptor の申告値で、電源回路を制御する値ではありません。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[CustomDevice.ino](CustomDevice.ino)
