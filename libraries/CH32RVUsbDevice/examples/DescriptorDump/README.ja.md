# DescriptorDump

English: [README.md](README.md)

ホストに見せている記述子(デバイス、構成、BOS、Microsoft OS 2.0、文字列、HID のレポート)と、ホストが送ってきた
制御要求、バスのイベントを、UART に表示します。ドライバが結び付かないときなどに、何をやり取りしたかを見る例です。

## 必要なものと配線

PC と、115200 bps・8N1 の UART モニタ。

## 操作と期待する結果

1. UART モニタを開き、USB を PC に接続します。
2. 列挙の制御要求が `ACK  80 06 0100 0000   64 -> ok 18` のように並び、`bus: configured` のあとに記述子が表示されます。
3. `d` を送ると、記述子をもう一度表示します。

## 設定と制約

- 記述子は `deviceDescriptor()` / `configurationDescriptor()` などで取り出します。このライブラリは記述子を常駐させず、
  呼ばれたときに 1 つの共有のバッファへ組み立てます。返したポインタは、次に同じ種類の API を呼ぶまで有効です。
- `onAnyControlRequest()` は、ベンダー要求を SETUP の段で、すべての制御転送を完了(ACK)の段で知らせます。
  ACK の行の最後の数字は、デバイスが返したバイト数です。
- コールバックは、`loop()` の先頭の `deliverEvents()` から呼ばれます。それまでに溜められる要求は 8 個で、超えた分は
  捨てられます（`USBDevice.droppedControlRequests()`）。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)

[DescriptorDump.ino](DescriptorDump.ino)
