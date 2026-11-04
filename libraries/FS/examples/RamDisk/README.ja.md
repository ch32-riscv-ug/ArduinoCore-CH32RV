# RamDisk

English: [README.md](README.md)

32 KB の RAM に FAT のボリュームを作り、ファイルを書き、途中でディレクトリを作りながらログに追記し、
全体を一覧してからログを読み戻します。

## 必要なものと接続

RAM が 48 KB 以上のボード、シリアルモニタ（`Serial`）。

## 手順と期待する結果

115200 baud でモニタを開いてリセットします。FAT の種類、大きさと空き、ツリー（`config.json`、`logs/` と
その中の `today.txt`）、ログの 3 行、使った量が表示されます。

## 設定と制限

64 ブロック（32 KB）が FatFs で作れる最小のボリュームで、FAT12 になります。リセットで消えます。小さい系列
（X035 の級）は `format()` が入っていないので、`begin(disk, true)` でボリュームを作れません。

## API の使い方と応用

| API | この例での使い方 |
|---|---|
| `fs::RamBlockDevice(storage, blocks)` | RAM の配列をブロックにする。どの `fs::BlockDevice` でも同じ |
| `fat.begin(disk, true)` | マウントし、ボリュームが無ければフォーマットする |
| `fat.open(path, FILE_APPEND, true)` | `true` で、無い `/logs` を作る |
| `dir.openNextFile()` | ディレクトリを一覧する。`e.path()` でサブディレクトリを開く |
| `fat.end()` | 外す。書いた分が書き出される |
