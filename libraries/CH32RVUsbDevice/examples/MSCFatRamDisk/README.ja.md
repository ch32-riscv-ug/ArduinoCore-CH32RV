# MSCFatRamDisk

English: [README.md](README.md)

10 KiB の RAM に FAT12 を作り、PC が書いた `CONFIG.TXT` を取り出す例です。

## 必要なものと配線

十分な RAM のある USB Device 対応ボード、PC のファイル操作、UART モニタ。

## 操作と期待する結果

1. PC でボリューム `CH32RV` と `README.TXT` を確認します。
2. 短い `CONFIG.TXT` をボリュームのルートへコピーします。
3. PC から安全な取り外し/eject を行います。
4. UART の `MSC_EJECT` に続く `CONFIG_SIZE`、`CONFIG_BEGIN`～`CONFIG_END` を確認します。

## 設定と制約

コピー後の OS キャッシュを確定するため eject が必要です。サンプルは内容の先頭255 bytesまでを表示します。容量10 KiBには FAT 等の管理領域も含まれ、すべてをファイルに使えるわけではありません。ファイル名はこの小さな FAT 実装の8.3形式に合わせます。内容は reset ごとに作り直します。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[MSCFatRamDisk.ino](MSCFatRamDisk.ino)
