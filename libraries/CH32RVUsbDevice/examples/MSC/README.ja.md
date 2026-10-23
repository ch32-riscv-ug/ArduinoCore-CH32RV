# MSC

English: [README.md](README.md)

RAM を16×512 bytesの raw block device として公開します。SCSI やブロック転送の確認用です。

## 必要なものと配線

USB Device 対応ボード、PC、UART モニタ。ディスクだけで8 KiBの RAM を使用します。

## 操作と期待する結果

1. UART で `blocks=16 block_size=512 bytes=8192` を確認します。
2. PC で容量の小さな USB マスストレージとして認識されることを確認します。
3. 必要ならブロック読み出しツールで先頭 sector の末尾 `55 aa` を確認します。

## 設定と制約

**FAT は作っていません。** ファイルブラウザで開けないのは想定どおりです。ファイルの例は [MSCFatRamDisk](../MSCFatRamDisk/README.ja.md) を使います。電源断・reset で内容は失われます。PC がフォーマットを提案しても、この例の確認には不要です。

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `CH32RVUsbDeviceMscRamDisk disk(storage, blocks, size)` | スケッチ所有の RAM 配列を、ブロック数と1ブロックのバイト数で指定します。 |
| `disk.attach(msc)` | MSC の読み書き先と容量を結び付けます。`false` のときは USB 起動へ進みません。 |
| `msc.mediaPresent(true)` / `msc.isWritable(true)` | 媒体があることと書き込み可能であることをホストへ示します。 |
| `msc.onStartStop(...)` | ホストの START STOP UNIT に応答します。戻り値は要求を受け入れるかどうかです。 |

`storage` は USB が公開している間ずっと必要です。ブロック数を増やすときは配列長も
`blockCount × blockSize` にそろえ、ほかの処理が使う RAM も残します。reset すると内容は失われます。

独自媒体へ置き換える場合は `onRead()` / `onWrite()` または storage オブジェクトを使います。
読み書き位置は `lba × blockSize + offset` で求め、要求の `size` 分を処理した長さを返します。
これらと `onStartStop()` は USB が答えを待つ処理なので、通知 callback と違い、長い待ちや `delay()` を入れません。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[MSC.ino](MSC.ino)
