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

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| 各 function の constructor | 同じ `usb` を渡し、CDC・DFU・HID をデバイスへ登録します。使用中のオブジェクトを `setup()` のローカル変数にはしません。 |
| `CH32RVUsbDeviceConfig` | USB の識別情報を設定します。設定を済ませてから `usb.begin(config)` を呼びます。 |
| `data.onOutputReport()` / `data.sendInput()` | 受信 payload を返信します。返信の成功確認を追加する場合は `sendInput()` の `bool` を調べます。 |
| `usb.mounted()` | ホストがデバイスを構成した状態を読みます。CDC の端末が開いているかとは別です。 |

製品名や serial number に動的な文字列を渡す場合は、USB が使っている間、その文字列の保存先を有効に保ちます。
`String(...).c_str()` のような一時オブジェクトのアドレスは使わず、文字列リテラルや寿命の長い配列を使います。

この例での CDC と HID は独立した通信路です。同じプロトコルを載せるなら、両方に対して長さ検査・応答形式・
失敗時の扱いを定めます。列挙の成功だけで双方向通信の確認を済ませず、それぞれに実際のデータを送って確認します。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[CustomDevice.ino](CustomDevice.ino)
