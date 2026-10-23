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

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `vendor.sendInput(payload, length)` | Device→Host の Input report を送ります。ここで渡す配列は Report ID を含まない payload です。 |
| `onOutputReport()` | Host→Device の Output report を `deliverEvents()` で受け取ります。 |
| `onFeatureReport()` | ホストから設定された Feature report を受け取ります。自動で設定を永続化する機能ではありません。 |
| `report.reportId` / `report.length` / `report.data` | report の識別子・payload 長・データです。受信配列を文字列と仮定しません。 |

ホスト側 API は送信バッファの先頭に Report ID を要求することがあります。descriptor とホスト側 API の規則を確認し、
Device の `sendInput()` へ ID を二重に含めないようにします。callback で受けた `data` をあとで使うなら自分の領域へコピーします。

新しいコマンドを足す場合は、先頭にコマンド番号や長さを定義し、`report.length` を検査してから読みます。
受信 report は NUL 終端とは限らないため、そのまま `%s` に渡しません。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[VendorHID.ino](VendorHID.ino)
