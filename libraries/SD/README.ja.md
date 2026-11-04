# SD — SPI でつなぐ SD カード（ESP32 の SD の API）

[English](README.md)

`SD` は、SPI モードの SD カードの上の `FatFS`（[FS ライブラリ](../FS/README.ja.md)）です。`SD.open()` や
`File` などは FS ライブラリのものです。

```cpp
#include <SD.h>

void setup() {
    SPI.begin();                          // ほかの SPI のデバイスと同じくバスを始める
    if (!SD.begin(PA4)) {                 // カードのチップセレクト
        return;                           // カードが無い、FAT のボリュームが無い、配線
    }
    File f = SD.open("/log.txt", FILE_APPEND);
    f.println(millis());
}
void loop() { }
```

## API

| | |
|---|---|
| `begin(ssPin = SS, spi = SPI, frequency = 4000000, mountpoint, max_files, format_if_empty = false)` | カードを識別してボリュームをマウントする。バスはスケッチが始める。`frequency` は 400 kHz の識別のあとのクロック（SPI モードの SD カードは 25 MHz まで。実際は配線で決まる）。`mountpoint` と `max_files` は ESP32 の引数で、使わない。`format_if_empty` は FAT のボリュームが無いカードをフォーマットする |
| `end()` | 外す |
| `cardType()` | `CARD_SD`（SDSC）、`CARD_SDHC`（SDHC / SDXC）、`CARD_MMC`、`CARD_NONE` |
| `cardSize()`、`numSectors()`、`sectorSize()` | カードの CSD から |
| `readRAW(buffer, sector)`、`writeRAW(buffer, sector)` | ファイルシステムを通さず 512 バイトのブロックを 1 つ |
| `card()` | `SDSPIBlockDevice` そのもの |

`SDSPIBlockDevice` は単独の `fs::BlockDevice` としても使えます（別の `FatFS`、または
`CH32RVUsbDeviceMscBlockStorage` でカードを PC に見せる）。

## ドライバ

- SDSC（v1・v2）、SDHC、SDXC、MMC。コマンドとデータのブロックはすべて CRC 付き（CMD59）。`build_opt.h` の
  `-DSD_SPI_CRC=0` で外せる。
- 複数のブロックを CMD18 / CMD25 でまとめて読み書きする。書き込みは、カードの書き込みの完了を待ってから状態を
  聞く（CMD13）。カードが受け取ったが書けなかった書き込みは失敗になる。
- どの `SPIClass` でも動く（ハードウェアの SPI、SoftSPI）。
- 確かめていないもの: カードの書き込み保護のスイッチ、マウント中のカードの抜き取り。

## 確かめたこと

ホストの試験（tests/unit/fs の `test_sd_spi_*`）で、コマンドとブロックの CRC を検査する模擬のカードを相手に
動かしています。SDHC、SDSC v2・v1 の識別とフォーマット、複数ブロックの転送でのファイルの読み書き、壊れた CRC と
断られた書き込みの検出、そのあとの `fsck` がきれいなこと、カードの無いバスで `begin()` が失敗することです。
チップではビルドだけです。
