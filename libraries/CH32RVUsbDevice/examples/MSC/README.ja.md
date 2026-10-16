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

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[MSC.ino](MSC.ino)
