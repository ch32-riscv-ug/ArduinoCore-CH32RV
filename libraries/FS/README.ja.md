# FS — FAT 上のファイルとディレクトリ（FatFs、ESP32 の FS / File の API）

[English](README.md)

`FatFS` は、ブロックデバイスの上の FAT12/16/32・exFAT のボリュームをマウントし、ESP32 の `FS` / `File` の
API で使えるようにします。`open()`、`exists()`、`remove()`、`rename()`、`mkdir()`、`rmdir()`、`File` の
`Stream`、`seek()`、`openNextFile()` です。ファイルシステムは FatFs R0.16 に作者の patch 1・2
（CVE-2026-6682・-6683・-6687 の修正）を当てたものです。

```cpp
#include <FatFS.h>

static uint8_t storage[64 * 512];
fs::RamBlockDevice disk(storage, 64);
FatFS fat;

void setup() {
    fat.begin(disk, true);                      // ボリュームが無ければフォーマットする
    File f = fat.open("/logs/today.txt", FILE_APPEND, true);   // true: /logs を作る
    f.printf("%lu\n", millis());
    f.close();
}
void loop() { }
```

## ボリュームを置く先

`fs::BlockDevice`（FSBlockDevice.h）: 番号の付いた 512 バイトのブロック。

| 媒体 | ブロックデバイス |
|---|---|
| RAM | `fs::RamBlockDevice`（このライブラリ）。64 ブロック（32 KB）以上 |
| PC に見せる USB のマスストレージ | 同じブロックデバイスを `CH32RVUsbDeviceMscBlockStorage`（CH32RVUsbDevice）で。ボリュームを持つのは一度にどちらか一方だけ: [MSCFileSystem](../CH32RVUsbDevice/examples/MSCFileSystem/README.ja.md) |
| SD カード（SPI、SDIO）、eMMC、USB ホストの USB メモリ | まだ無い（docs/filesystem.ja.md） |

ほかの媒体は、スケッチで `fs::BlockDevice` を実装すれば使えます。`readBlocks()`、`writeBlocks()`、
`blockCount()` と、必要なら `begin()`、`sync()`、`writable()`、`eraseBlocks()` です。

外付けの SPI NOR Flash は、ここではファイルシステムの媒体にしません。生のイメージとして使い、ファイルは
SD カードに置きます（docs/filesystem.ja.md）。

## API

`FatFS`:

| | |
|---|---|
| `begin(device, formatIfEmpty = false)` | マウントする。`formatIfEmpty` は、FAT のボリュームが無い媒体をフォーマットする（`format()` が入っている系列で） |
| `end()` | 外す。開いているファイルは閉じて書き出し、その `File` はどれも使えなくなる |
| `format()` / `format(device)` | 新しい空のボリューム。大きさで FAT12/16/32、約 4 GB から exFAT。32 MB から MBR の区画を作り、それより小さいと区画なし |
| `mounted()`、`fatType()`、`totalBytes()`、`usedBytes()`、`freeBytes()` | |

`FS` と `File` は ESP32 と同じです。パスは絶対パス（`/dir/file.txt`）。モードは `"r"`、`"r+"`、`"w"`、
`"w+"`、`"a"`、`"a+"` で、`"r"` はディレクトリも開きます。`open(path, mode, true)` は、書くために開く
ファイルの、無い親ディレクトリを作ります。

ESP32 と違うところと、その理由:

- **`"w"` は、古いファイルを `open()` ではなく `flush()` / `close()` で切り詰める。** 古い中身の上に書くので、
  その場で書き換える小さいファイル（512 バイトより小さい設定）は、電源断のあとで古いか新しいかのどちらかに
  なり、空にはならない。切り詰めが済むまで、`size()` と `read()` は新しいファイルの分だけを見る。
- **終わりより先への `seek()` は失敗する**（FatFs は、書くために開いたファイルを伸ばしてしまう）。
- **`rename()` は行き先があると失敗し、`mkdir()` はディレクトリがあると失敗する**（FatFs と同じ）。FAT には
  置き換えのリネームが無い。
- `File` の扱い: コピーは 1 つの開いたファイルを共有し、最後のコピーが閉じる。`close()` はすべてのコピーに
  ついて閉じ、そのあとで使ったコピーは何もしない。
- `getLastWrite()` は FAT のローカル時刻（2 秒刻み）を `mktime()` で変換したもの。タイムスタンプの無い構成
  （小さい系列）では 0。新しいファイルには `time()` をローカル時刻として付け、時計を一度も合わせていなければ
  2026-01-01。

## ビルド時の大きさ

スケッチの `build_opt.h` で変えられます（`-DFF_FS_EXFAT=0`）。

| | 既定 | |
|---|---|---|
| `FS_MAX_OPEN_FILES` | 4（V003: 2） | 同時に開けるファイルとディレクトリの数（全ボリュームで）。`e = dir.openNextFile()` の一覧は 3 つ使う |
| `FS_PATH_MAX` | 128（X035 の級: 64、V003: 40） | `File` が持つパスの長さの上限。長いパスは開けない |
| `FF_VOLUMES` | 2（小さい系列: 1） | 同時にマウントできるボリューム |
| `FF_FS_EXFAT`（と GPT） | あり（小さい系列: なし） | +6.5 KB |
| `FF_FS_NORTC` | なし（小さい系列: あり） | `time()` からのタイムスタンプ: +5.7 KB（`mktime()` / `localtime()`） |
| `FF_USE_MKFS` | あり（小さい系列: なし） | `format()` |
| `FF_FS_TINY` | なし（小さい系列: あり） | ファイルがボリュームのセクタのバッファを共有する。1 つ 600 B が 50 B になる |
| `FF_USE_LFN` | あり（V003: なし） | 長い名前（UTF-8）。なしでは 8.3 の名前 |
| `FF_FS_READONLY` | なし | 読み出しだけ |

小さい系列は V002〜V007、M007、X033、X035、M030 です。

空のスケッチに対する Flash と RAM（マウント、追記、読み戻し。docs/filesystem.ja.md）:

| | Flash | RAM |
|---|---|---|
| CH32V307、既定 | +27.1 KB | +5.7 KB |
| CH32V307、exFAT なし | +20.6 KB | +4.7 KB |
| CH32V307、exFAT・タイムスタンプ・フォーマット・ロックなし、ボリューム 1 つ | +14.3 KB | +4.4 KB |
| CH32X035、既定 | +14.0 KB | +2.1 KB |
| CH32X035、読み出しだけ | +9.0 KB | +2.1 KB |
| CH32V003、既定（8.3 の名前） | +10.3 KB | +1.3 KB |
| CH32V003、読み出しだけ | +6.1 KB | +1.3 KB |

RAM はスケッチの試験用の 512 バイトのバッファを含みます。

## 確かめたこと

ホストの試験（tests/unit/fs）が、ライブラリを PC 向けに、アドレスと未定義動作のサニタイザー付きでビルドします。
API の振る舞い、Linux の `mkfs.fat` / `mkfs.exfat` が作ったボリューム（FAT12/16/32、exFAT。MBR の区画あり・
なし）に書いて `fsck` がきれいと判定し Linux が名前を読めること、ライブラリがフォーマットしたボリューム、
2 つのボリュームの同時のマウントを、大きい系列と小さい系列の構成で確かめます。

チップでは（tests/peer/usb の usb-peer-fs、CH32X035F8U6 と ESP32-S3）、FatFS がフォーマットして書いたボリュームを
USB のマスストレージで見せ、S3 が ESP-IDF の FAT でマウントして読み、ファイルを書きます。DUT が取り戻して、自分の
ファイル（長い名前のディレクトリとファイルを含む）が残っていることと S3 のファイルを確かめ、ファイルをその場で
書き換えて、S3 が新しい中身を読みます。2 往復とも通りました（2026-11-04）。
