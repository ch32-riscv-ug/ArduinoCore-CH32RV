# MSCFileSystem

English: [README.md](README.md)

スケッチと PC で 1 つの FAT のボリュームを使います。スケッチが `FROM_CH32.TXT` を書いてから、ボリュームを
USB のドライブとして PC に見せます。PC が取り外すと、スケッチが取り戻して中身（PC のファイルも）を一覧し、
もう一度見せます。

## 必要なものと接続

USB デバイスがあり RAM が 48 KB 以上のボード。その USB を PC に、UART のモニタを `Serial`（`RX` / `TX`）に。

## 手順と期待する結果

1. リセットします。UART に `FILES`（最初は空）と `SHOWN_TO_PC` が出ます。
2. PC でドライブを開くと、`FROM_CH32.TXT` に 1 行あります。ファイルを 1 つコピーします。
3. PC でドライブを取り外します。UART に `EJECTED`、PC のファイルと `FROM_CH32.TXT` の並んだ `FILES`、
   `SHOWN_TO_PC` が出て、`FROM_CH32.TXT` が 1 行増えたドライブが戻ってきます。

## 設定と制限

ボリュームを持つのは、一度にどちらか一方だけです。どちらも FAT のセクタをキャッシュに持ち、相手の書き込みを
見ないためです。スケッチは、PC が持っていない間（`mediaPresent(false)`）だけマウントし、外してから見せます。
PC が取り外さなければ、PC が持ったままです。32 KB のボリュームは FAT12 で、リセットで消えます。

## API の使い方と応用

| API | この例での使い方 |
|---|---|
| `CH32RVUsbDeviceMscBlockStorage shared(disk)` | FatFS がマウントするのと同じブロックデバイスから MSC を返す |
| `shared.attach(msc)` | ブロックの数・大きさ・書き込めるかをデバイスから取る |
| `shared.onEject(...)` | PC が取り外した。`deliverEvents()` から呼ばれる |
| `msc.mediaPresent(true / false)` | 媒体を PC に渡す・取り戻す |
| `fat.begin()` / `fat.end()` | PC が持っていない間だけマウントする |
