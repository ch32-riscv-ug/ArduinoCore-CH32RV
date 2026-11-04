# SD_MMC — SDIO でつなぐ SD カード（ESP32 の SD_MMC の API）

[English](README.md)

`SD_MMC` は、SDIO のホストにつないだ SD カードの上の `FatFS`（[FS ライブラリ](../FS/README.ja.md)）です。
CH32V303 / V305 / V307 / V317（SDIO のあるパッケージ）、V407 / V467、H415 / H417 で使えます。それ以外では
`begin()` が false です。

```cpp
#include <SD_MMC.h>

void setup() {
    if (!SD_MMC.begin()) {                // 系列の既定のピン、データ線 4 本
        return;
    }
    File f = SD_MMC.open("/log.txt", FILE_APPEND);
    f.println(millis());
}
void loop() { }
```

## ピン

| 系列 | CK | CMD | D0 | D1 | D2 | D3 |
|---|---|---|---|---|---|---|
| V30x（固定） | PC12 | PD2 | PC8 | PC9 | PC10 | PC11 |
| V4x7 | PC12 | PD2 | PC8 か PB14 | PC9 か PB15 | PC10 | PC11 |
| H417 | PB11 か PC1 | PB10 か PA0 | PE8 か PB14 | PE9 か PB15 | PE10 か PB3 | PE11 か PB4 |
| H415 | PB11 か PC1 | PB10 か PA0 | PB14 | PB15 | PB3 | PB4 か PE11 |

それぞれ最初のものが既定です（H417 は EVT のもの）。ほかは `begin()` の前に `setPins()` で選びます。V4x7 では
D0 と D1 が一緒に動きます。CMD と D0〜D3 にはプルアップ（10〜47 kΩ）を付けてください。端子はプッシュプルの出力と、
プルアップの無い入力になります。CH32V307V-R2 の評価ボードには、V30x のピンにスロットがあります。

## API

| | |
|---|---|
| `setPins(clk, cmd, d0)` / `setPins(clk, cmd, d0, d1, d2, d3)` | `begin()` の前に。3 本なら 1 ビット。系列がそこに SDIO を出せないときは false |
| `begin(mountpoint, mode1bit = false, format_if_mount_failed = false, sdmmc_frequency = 20000, maxOpenFiles)` | カードを識別してマウントする。`sdmmc_frequency` は kHz の上限。クロックは HCLK / n で、最大 25 MHz（default speed。high speed には切り替えない）。`mountpoint` と `maxOpenFiles` は ESP32 の引数で、使わない |
| `end()`、`cardType()`、`cardSize()`、`numSectors()`、`sectorSize()`、`readRAW()`、`writeRAW()` | SD と同じ |
| `card()` | `SDIOBlockDevice`: `clockHz()`、`wide()` |

## ドライバ

- 識別は 1 ビットで、分周器で出せるいちばん遅いクロック。HCLK が 103 MHz までは 400 kHz、それより上は HCLK / 257
  （144 MHz で 560 kHz。規格の 400 kHz より速いが、カードは実際には答え、EVT も同じ）。そのあと 4 本（ACMD6）と
  転送のクロックにする。
- FIFO は CPU が動かし、ハードウェアのフロー制御を使う。FIFO が満杯（読み）や空（書き）の間はクロックが止まるので、
  途中の割り込みは時間がかかるだけでデータは失わない。DMA は使わない。
- SDSC、SDHC、SDXC。MMC は 1 ビット。
- チップで動かしていないもの: タイミング、100 MHz を超える HCLK、H41x の AF のピン、V4x7 のリマップ、WCH の
  ブロックでのハードウェアのフロー制御（STM32F1 のものには errata がある）。

## 確かめたこと

V303、V307、V317、V407、H415、H417 向けにビルドだけ通しています。チップでは動かしていません。
