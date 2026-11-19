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

| 系列 | CK | CMD | D0 | D1 | D2 | D3 | D4〜D7（MMC の 8 ビット） |
|---|---|---|---|---|---|---|---|
| V30x（固定） | PC12 | PD2 | PC8 | PC9 | PC10 | PC11 | PB8、PB9、PC6、PC7 |
| V4x7 | PC12 | PD2 | PC8 か PB14 | PC9 か PB15 | PC10 | PC11 | PB8、PB9、PC6 か PA9、PC7 か PA10 |
| H417 | PB11 か PC1 | PB10 か PA0 | PE8 か PB14 | PE9 か PB15 | PE10 か PB3 | PE11 か PB4 | PE12〜PE15 か PB8、PB9、PC6、PC7 |
| H415 | PB11 か PC1 | PB10 か PA0 | PB14 | PB15 | PB3 | PE11 か PB4 | PE12〜PE15 か PB8、PB9、PC6、PC7 |

表は variant の `CH32RV_SDIO_PADS`（ch32-device-data から生成）で、それぞれ最初のものが既定です（H417 は EVT のもの）。
ほかは `begin()` の前に `setPins()` で選びます。H41x は端子ごとに AF を選ぶので自由に組み合わせられます。V4x7 は
PCFR2 の SDIO_RM 1 ビットで D0・D1・D6・D7 が一緒に動くので、同じ側の組だけです。H416 は device-data に D0 の
端子が無く、`begin()` が false です。CMD と D0〜D7 にはプルアップ（10〜47 kΩ）を付けてください。端子はプッシュプルの
出力（H41x は出力の速さを最速）と、プルアップの無い入力になります。CH32V307V-R2 の評価ボードには、V30x のピンに
スロットがあります。

V30x と V4x7 では、Ethernet の MAC のクロック（RCC の ETHMACEN）が入っていると、D0・D1 が勝手に PB14・PB15 に
移ります（V30x はロットによる。データシートの注）。このライブラリは MAC のクロックを入れてその道を使うことはしません。
ETH と SD_MMC を一緒に使うときは、V4x7 では `setPins()` で PB14・PB15 の組にしてください。

## API

ESP32 の SD_MMC と同じ名前と形です。

| | |
|---|---|
| `setPins(clk, cmd, d0)` / `setPins(clk, cmd, d0, d1, d2, d3)` | `begin()` の前に。3 本なら 1 ビット。系列がそこに SDIO を出せないときは false |
| `setPins(clk, cmd, d0, ..., d7)` | 8 ビット（MMC / eMMC）。ESP32 には無い、このコアの足し |
| `begin(mountpoint, mode1bit = false, format_if_mount_failed = false, sdmmc_frequency = BOARD_MAX_SDMMC_FREQ, maxOpenFiles)` | カードを識別してマウントする。`sdmmc_frequency` は kHz の上限で、既定は ESP32 と同じ `SDMMC_FREQ_HIGHSPEED`（40000）。default speed（SD 25 MHz、MMC 26 MHz）を超える上限のときは high speed に切り替える（SD は CMD6 で 50 MHz、MMC は HS_TIMING で 52 / 26 MHz）。クロックは HCLK / n で、上限とカードの上限の低い方の下。`mountpoint` と `maxOpenFiles` は ESP32 の引数で、使わない |
| `end()`、`cardType()`、`cardSize()`、`numSectors()`、`sectorSize()`、`readRAW()`、`writeRAW()` | SD と同じ |
| `card()` | `SDIOBlockDevice`: `clockHz()`、`busWidth()`（1 / 4 / 8）、`wide()`、`highSpeed()`、`dma()`、`dmaBlocks()` / `cpuBlocks()`（`begin()` からのブロック数）。`begin()` の前に `setDMA(false)`（すべて CPU で）、`setFlowControl(false)`（ハードのフロー制御を切る） |

ESP32 との違い: ESP32 は 1 / 4 ビットだけで、8 ビットの `setPins()` はありません。ESP-IDF は上限が 20 MHz を超えると
high speed を試し、SD の high speed を 40 MHz（ESP の分周）で動かします。ここでは 25 MHz を超える上限で試し、
カードの上限（50 MHz）までです。どちらも規格の範囲で、違いは分周器の都合です。

## ドライバ

- 識別は 1 ビットで、分周器で出せるいちばん遅いクロック。HCLK が 103 MHz までは 400 kHz、それより上は HCLK / 257
  （144 MHz で 560 kHz。規格の 400 kHz より速いが、カードは実際には答え、EVT も同じ）。
- SD: ACMD6 で 4 本、上限が 25 MHz を超えれば CMD6 の SWITCH_FUNC で high speed（関数 1 があるかを mode 0 で聞いて、
  mode 1 で切り替え、切り替わった関数を状態で確かめる）。
- MMC: CMD1 で識別（セクタのアドレスを求める）。CSD の SPEC_VERS が 4 以上なら EXT_CSD（CMD8）を読み、セクタの
  アドレスのカードの大きさは SEC_COUNT から。BUS_WIDTH（CMD6）で 4 / 8 本にし、新しい幅で EXT_CSD を読み直して
  確かめる（読めなければ 1 本に戻す）。CARD_TYPE が許せば HS_TIMING で high speed。
- ブロックは DMA（V30x・V4x7 は DMA2 の 4 番、H41x は DMAMUX で同じ DMA2 の 4 番に SDIO の要求）で FIFO と
  メモリの間を 1 ワードずつ。チャネルは `begin()` で `ch32rv_dma_claim()` で取り、ほかが持っていれば CPU で動かす。
  4 バイト境界でない buffer（FatFS が自分の buffer を使う場合など）も CPU。
- ハードウェアのフロー制御は既定で on。FIFO が満杯（読み）や空（書き）の間はクロックが止まるので、途中の割り込みや
  DMA の待ちは時間がかかるだけでデータは失わない（WCH の RM は BYPASS のときに HWFC_EN を要ると書いている）。
- SDSC、SDHC、SDXC、MMC / eMMC。

大きさ（CardInfo の例、clang、2026-11-17）: DMA・high speed・MMC の幅を足して、text が V307 で 35664 → 36928（+1264）、
V407 で 35456 → 36776（+1320）、H417 で 37072 → 38432（+1360）バイト。SD_MMC を使わないスケッチは変わりません。

## 確かめたこと

V303、V307、V317、V407、H415、H417 向けにビルドを通しています。4 ビット・20 MHz の読み書きは H417（nanoCH32H417 の
SD の口）で確かめています（その前のポーリングの版）。DMA・high speed・フロー制御なし・1 ビットの比べは
tests/instrumented/sd（test_sd_sdio_transfer、test_sd_sdio_high_speed）、MMC は単発（tests/TEST_PLAN.ja.md）で、
まだ流していません。
