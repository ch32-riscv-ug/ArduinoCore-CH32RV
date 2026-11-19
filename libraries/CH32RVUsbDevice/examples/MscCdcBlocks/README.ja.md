# MscCdcBlocks

[English](README.md)

8 KB の生のブロックの機器（ファイルシステム無し、リセットで消える）と、その横に、ホストが何回読み書きしたかを知らせる
シリアルのポートを持つ複合機器です（EspUsbDevice の MscCdcBlocks と同じ）。

## 必要なものと配線

USB デバイスのある石と、PC への USB の線。失敗の表示は 115200 bps の UART モニタ（`RX` / `TX`）。

## 操作と期待する結果

PC には 8 KB のドライブ（`CH32RV` / `RAM blocks`）と、シリアルのポート（インターフェース名 `disk statistics`）が見えます。
ドライブは中身がすべて 0 で、OS は初期化（フォーマット）を勧めます。シリアルを開くと毎秒 `reads=... writes=...` が出て、
`?` を送るとすぐに出ます。PC がドライブを読み書きすると数が増えます。

## API と処理の流れ

| API | この例での使い方 |
|---|---|
| `disk.onRead()` / `onWrite()` | ブロックの読み書き。ホストが答えを待つ間（USB の処理の中）に呼ばれるので短く。範囲の外は -1。 |
| `disk.begin(blocks, size)` | ブロックの数と大きさ。 |
| `disk.vendorID()` / `productID()` / `mediaPresent(true)` | ホストに見せる名前と、媒体があること。 |
| `CH32RVUsbDeviceCdcSerial console(device, "disk statistics")` | 名前つきのシリアル。数は読み書きの関数の呼ばれた回数で、バイト数ではない。 |

ファイルとして扱うなら [MSCFatRamDisk](../MSCFatRamDisk/README.ja.md)、残すなら SD などの記憶を `onRead()` / `onWrite()` に置きます。

CH32H417 の SuperSpeed で確かめました（2026-11-20、PyUSB から Bulk-Only Transport で SCSI）: 512 バイトの 16 ブロック、書いた
ブロックがそのまま読めて、シリアルの `?` が書き込みと読み出し（OS が載せたときの読み出しを含む）を数えていました。
