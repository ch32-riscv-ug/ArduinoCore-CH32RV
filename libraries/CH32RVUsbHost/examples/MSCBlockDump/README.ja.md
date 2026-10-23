# MSCBlockDump

English: [README.md](README.md)

USB メモリの INQUIRY と容量を表示し、先頭ブロックを hex dump します。ファイルシステムは使いません。

## 必要なものと配線

媒体が入った USB メモリ/カードリーダー、Host の VBUS、115200 bps UART モニタ。

## 操作と期待する結果

1. 接続して ready になると vendor/product、block 数、block size が表示されます。
2. block size が512の場合、LBA 0を512 bytes読み、16 bytesずつ hex 表示します。
3. 抜いて再接続すると、再び一度だけ表示します。

## 設定と制約

この例は読み出しのみです。512 bytes以外の媒体では容量だけ表示し、bufferサイズに合わない読み出しを行いません。先頭ブロックは partition table の場合も、filesystem の boot sector の場合もあります。媒体のないカードリーダーでは準備が完了しないことがあるため、媒体を入れて試します。

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `mscReady()` / `mscWaitReady()` | MSC 機能が使えることと、媒体が準備できることを確認します。後者は待つ場合があります。 |
| `mscInquiry(inquiry)` | 媒体の vendor/product 等を受け取ります。 |
| `mscCapacity(blocks, size)` | ブロック数と1ブロックのバイト数を受け取ります。総容量は両者の積です。 |
| `mscReadBlocks(lba, buffer, count)` | 指定 LBA から count ブロックを読みます。count はバイト数ではありません。 |

読み出し先は `count × blockSize` bytes必要です。容量計算では大きい媒体でも桁あふれしない型を使います。
この例が block size と配列長の一致を確認するのは、配列を超えた読み出しを避けるためです。

先頭ブロックはファイルの先頭ではありません。ファイルへアクセスするには partition とファイルシステムの処理が
別途必要です。読み出し失敗を再試行したい場合は、`dumped` が処理の前に立つ点も変更し、回数や期限を設けます。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbHost の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[MSCBlockDump.ino](MSCBlockDump.ino)
