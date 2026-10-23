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

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `disk.format(label)` | RAM 上に FAT ボリュームを作ります。reset 時に作り直され、PC が保存した内容も消えます。 |
| `addTextFile(name, text)` / `attach(msc)` | 初期ファイルを作り、MSC の媒体として公開します。改造時は両方の成否を確認します。 |
| `disk.onEject(...)` | ホストの取り外し通知を `deliverEvents()` で受け、読み取り要求のフラグを立てます。 |
| `exists()` / `fileSize()` / `readFile()` | ファイルの存在・長さを調べ、実際に読み出したバイト数を受け取ります。 |

PC でファイルをコピーした直後は、まだホスト側に未書き込みのデータがある場合があります。
取り外し操作で書き込みを完了させてから、スケッチが内容を読みます。ホストが通知しない取り外し方では
callback が来ないこともあるため、単に USB ケーブルを抜く操作とは区別します。

読み出し先を文字列として使うときは、配列に終端1 byte分を残し、`readFile()` が返した位置へ `\0` を入れます。
この例は先頭部分だけを表示するため、大きいファイルの全内容を扱うなら `fileSize()` と取得長を比較します。
ファイル名とディレクトリの扱いは FAT RAM disk の API の制約に合わせます。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[MSCFatRamDisk.ino](MSCFatRamDisk.ino)
