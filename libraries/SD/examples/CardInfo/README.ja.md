# CardInfo

English: [README.md](README.md)

SPI でつなぐ SD カードの種類と大きさ、ファイルの書き込みと読み戻し、ルートの一覧、256 KB の書き込みと読み出しとその時間を
表示します。

## 必要なものと接続

ボードの SPI につないだ SD カード（バスの SCK・MISO・MOSI、チップセレクトは `SS`、SS の無い系列では PA4。ほかのピンは build_opt.h の `SD_CS_PIN` で）、FAT のボリュームのあるカード、シリアルモニタ。

## 手順と期待する結果

115200 baud でモニタを開いてリセットします。`card SDHC/SDXC, 30436 MB, FAT type 3, ...`、`read back: written at ...`、ルートのエントリー、
`256 KB: write ... ms, read ... ms (262144 bytes back)` が出ます。`..._BEGIN_FAILED` は、カードが無い、
FAT のボリュームが無い、配線のどれかです。

## 設定と制限

カードには `/ch32rv.txt` が残ります。`/speed.bin` は時間を測ったあとで消します。チップではまだ動かしていません。

## API の使い方と応用

| API | この例での使い方 |
|---|---|
| `begin()` | カードを識別し、FAT のボリュームをマウントする |
| `cardType()`、`cardSize()` | カードから |
| `fatType()`、`freeBytes()` | ボリューム（FatFS）から |
| `open()`、`openNextFile()`、`remove()` | ファイル。FS ライブラリと同じ |
