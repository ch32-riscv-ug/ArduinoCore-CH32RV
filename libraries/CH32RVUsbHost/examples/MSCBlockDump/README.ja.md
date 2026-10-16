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

この例は読み出しのみです。512 bytes以外の媒体では容量だけ表示し、bufferサイズに合わない読み出しを行いません。先頭ブロックは partition table の場合も、filesystem の boot sector の場合もあります。空のカードリーダーは列挙時の TEST UNIT READY に留まる既知の制約があるので、媒体を入れて試します。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbHost の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[MSCBlockDump.ino](MSCBlockDump.ino)
