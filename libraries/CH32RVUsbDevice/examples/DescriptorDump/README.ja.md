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
  呼ばれたときに 1 つの共有のバッファへ組み立てます。共有バッファを使うため、別の種類も含め、次に記述子を組み立てる API を呼ぶ前に使い切ります。
- `onAnyControlRequest()` は、ベンダー要求を SETUP の段で、すべての制御転送を完了(ACK)の段で知らせます。
  ACK の行の最後の数字は、デバイスが返したバイト数です。
- コールバックは、`loop()` の先頭の `deliverEvents()` から呼ばれます。保存できる通知には限りがあり、取りこぼしは
  `USBDevice.droppedControlRequests()` で確認します。

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `deviceDescriptor()` / `configurationDescriptor()` | device の識別と、interface/endpoint の構成を読みます。`nullptr` を確認してから参照します。 |
| `hidReportDescriptor()` | HID のデータ配置を読みます。configuration の HID descriptor と report descriptor は別のものです。 |
| `onAnyControlRequest()` | 制御要求の観察用通知です。要求へ応答する provider を登録する API ではありません。 |
| `onBusEvent()` / `deliverEvents()` | バス通知をスケッチ側で受け、構成完了をきっかけに dump します。 |
| `droppedControlRequests()` | 観察ログの取りこぼしを確認します。ログに無いことだけで要求が来ていないとは判断しません。 |

configuration、BOS、MS OS 2.0、HID report の取得は共有の作業バッファを使います。
**別の種類も含め、次の記述子を組み立てる API を呼ぶ前に、表示または自分の領域へのコピーを済ませます。**
文字列記述子も次の `stringDescriptor()` 呼び出しまでに使い切ります。

大きな dump の UART 表示は時間がかかります。通信のタイミング問題を調べる場合は表示内容を絞り、
ログ出力自体が動作へ与える影響も考えます。文字列表示は簡易表示なので、非 ASCII の製品名の検証には生データを使います。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)

[DescriptorDump.ino](DescriptorDump.ino)
