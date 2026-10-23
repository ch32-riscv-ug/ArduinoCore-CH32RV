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

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `onDeviceConnected()` | open を予約するフラグを立てます。クラス固有の処理は `loop()` へ分けます。 |
| `vendorOpen(address, interfaceNumber)` | vendor interface を選んで通信を開始します。省略時は候補を自動で選びます。 |
| `onVendorData(callback)` | IN のデータを `deliverEvents()` で受け取ります。ポインタを保持せず、必要ならコピーします。 |
| `vendorWrite(buffer, length)` | OUT へ送信し、成功を `bool` で返します。失敗時は `lastErrorName()` を確認します。 |

`onVendorData()` に渡る長さは一つのアプリメッセージの長さとは限りません。長さフィールドや区切り文字などで
プロトコルの境界を定義し、複数回の受信を組み立てられるようにします。IN/OUT の方向は Host 基準です。

接続通知は vendor 以外の機器でも発生します。目的の機器・interface を選び、`vendorOpen()` の結果を確認してから
UART 入力を受け付ける形にすると、未接続時の読み捨てを避けられます。一般用途では送信失敗時の再試行も用意します。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbHost の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[VendorBulk.ino](VendorBulk.ino)
