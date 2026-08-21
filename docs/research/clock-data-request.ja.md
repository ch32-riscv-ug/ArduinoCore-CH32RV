# R-24: クロック関連データの整備依頼(上流向け)

日付: 2026-08-21
状態: **依頼案。上流へ未提出**
関連: [R-20](register-map-data.ja.md)、[todo](../todo.ja.md)の「クロック」、[ADR-0012](../adr/0012-usb-stack.ja.md)

## なぜ要るか

`SystemInit`をPLL込みに一般化する([todo](../todo.ja.md))。
そのために必要な事実は**すべてfamilyごとに違い、いまはEVTを手で読んで写している**。
[R-23](tinyusb-vendor-header.ja.md)で「手写しのデータを土台にすると保守の当てが無い」と
整理したのと同じ問題なので、**先に上流(`ch32-device-data`)へ依頼を出しておきたい**。

`products.csv`にあるのはflash/sram容量とGPIO数まで。**クロックに関する表は1つも無い。**

## 実際に踏んだ「family差」(依頼の根拠)

EVTの`system_*.c`と`*_rcc.h`を読んで分かったこと。どれも推測ではない。

### 1. クロックツリーの段数が違う

| family | 形 | 例 |
|---|---|---|
| V003/V006、V103、V20x、V307、V205、L103、M030、X035 | SYSCLK = HCLK(AHB分周のみ) | — |
| **V407** | **SYSCLK ≠ HCLK** | `SYSCLK_400MHz_HCLK_200MHz_HSE` |
| **X315** | **SYSCLK / CoreCLK / HCLK の3段** | `SYSCLK_480M_CoreCLK_480M_HCLK_240M_HSI` |

いまのコアは`F_CPU = HCLK = HSI / AHB分周`の1段しか表現できない。

### 2. HSIから届く周波数がfamilyごとに違う

| family | HSI | HSI由来のSYSCLK選択肢(SDKが用意しているもの) |
|---|---:|---|
| V003 / V006 | 24 MHz | 8, 24, 48 |
| V103 | 8 MHz | 48, 56, 72 |
| V20x / V307 | 8 MHz | 48, 56, 72, 96, 120, 144 |
| V205 | 8 MHz | 40, 80, 120, 160, 192 |
| L103 | 8 MHz | 48, 56, 72, 96 |
| M030 | 8 MHz | 24, 48, 72 |
| X035 | 48 MHz | 8, 12, 16, 24, 48(**PLL無し。分周のみ**) |
| V407 | 20 MHz | 120, 240, 350, 400, 480 |
| X315 | 20 MHz | 240, 312.5, 480 |

### 3. PLLの制御がRCCの外にもある

V20xがHSIからPLLを回すとき、**`EXTEN`というWCH固有のレジスタ**を触る。

```c
EXTEN->EXTEN_CTR |= EXTEN_PLL_HSI_PRE;   /* HSIをPLLへ入れる前の分周の有無 */
```

「RCCだけ見ればよい」というモデルでは表現できない。

### 4. APB分周がSYSCLKに追随する

V20xは**HSI由来のどの周波数でも`PPRE1 = DIV2`**(APB1 = SYSCLK/2)、`PPRE2 = DIV1`。
つまりPLLを使った瞬間に**`PCLK1 = F_CPU`という前提が崩れる**。
このコアのUSART(BRR)、I2C(FREQ/CKCFGR)、SPI(BR)は現在その前提で書いてあるので、
**APB分周とAPB最大周波数はデータとして要る**。

### 5. 周辺には固有の上限がある

ADCは`ADCCLK = PCLK2 / (2,4,6,8)`で、**14 MHz以下**という制約がある(既にコード内に記録済み)。
SYSCLKを上げると分周を選び直す必要がある。

### 6. flash latencyの扱いがfamilyで違う

V20x / V30xの`system_*.c`は**flash latencyを一度も触らない**。
一方こちらは`CH32_FLASH_LATENCY`をfamily定数として持っている(X035=2、V006/V407/X315=1、他0)。
**HCLKを上げたときに何を設定すべきかがデータとして無い。**

## 依頼したいデータ(C-1〜C-8)

粒度は基本**family**。partごとに違うのは動作上限だけのはず。

| # | 欲しいもの | 粒度 | 具体例・列の案 |
|---|---|---|---|
| **C-1** | **クロックツリーの段構成**: どのドメインがあるか、固定の関係はあるか | family | `domains = SYSCLK,HCLK` / `HCLK = SYSCLK/2` |
| **C-2** | **発振器**: HSI周波数と確度、HSE許容範囲、LSI/LSE | family | `hsi_hz=8000000, hsi_tolerance_pct=1.0, hse_min=4000000, hse_max=25000000` |
| **C-3** | **PLL**: 入力源の選択肢、逓倍/分周のfield(register+bit+符号化)、VCO/出力の上下限、固定倍率か | family | V003は×2固定、V20xは`RCC_CFGR0.PLLMULL`で2〜18 |
| **C-4** | **PLL周辺の非RCCレジスタ**: `EXTEN_CTR`の`PLL_HSI_PRE`のような、PLLの挙動を変えるbit | family | **これが無いとV20xのHSI PLLは組めない** |
| **C-5** | **プリスケーラ**: AHB/APB1/APB2のfield符号化と、**各バスの最大周波数** | family | AHBは既に2通りの符号化があると判明済み(下記) |
| **C-6** | **flash latency**: HCLKに対する閾値と、設定するfield | family | `hclk<=24MHz -> 0, <=48 -> 1, ...` |
| **C-7** | **正確な周波数を要求する周辺のクロック経路**: USBの48MHz(源の選択肢と分周field)、ADCの上限と分周、RTC | family | V20xは`RCC_USBCLKSource_PLLCLK_Div1/1.5/2/3`、V307は`PLLCLK`か`USBPHY`、X035は不要 |
| **C-8** | 各行の**出典と確信度** | 全部 | 既存のremap系と同じ慣行で |

### C-5について: 一部は既にこちらで検証済み

AHBプリスケーラのfield符号化は**2通りある**ことを全11 familyのEVTヘッダで確認済み
([cores/arduino/ch32_clock.h](../../cores/arduino/ch32_clock.h))。

- linear(`0x0..0x7` = `/1../8`、`/3`や`/5`もある): V00x / M030 / X03x
- pow2(`0x8` = `/2`、`/32`が無い): V10x / V20x / V30x / V4x7 / L103 / V205 / X3x5

**この分は成果物ごと渡せる**ので、依頼の一部は「こちらの検証済みデータを取り込んでほしい」になる。

## 検証方法(これが無いと依頼は受け取りにくいはず)

[R-19](signal-name-normalization.ja.md)で効いたのは「独立に検証する手段があること」だった。
今回も同じ形が作れる。

EVTの`system_*.c`にある`SetSysClockTo<N>_HSI/HSE`は、**レジスタ書き込みの列そのもの**。
ここから`(family, 目標周波数) -> {PLLMULL, HPRE, PPRE1, PPRE2, EXTEN bits, latency}`を
機械的に抽出でき、依頼したデータと突き合わせられる。
`tools/generate/evt_remap_fields.py`がEVTのremap関数をホストで実行して検証したのと同じ発想で、
今回は**静的に読むだけで足りる**ぶん簡単。

さらに実機側では、**Serialが化けないこと**が「F_CPUと実際のHCLKが一致している」証明になる
(既にX035で`/2`と`/3`について確認済み)。

## 優先順位

1. **C-1 / C-3 / C-5 / C-6**を、実機のある**V003 / V103 / V20x / V307 / L103 / X035**から。
   これでPLL対応が動かせる
2. **C-4**(非RCCレジスタ)。V20xで実際に要るので1と同時
3. **C-7**のUSB分。[ADR-0012](../adr/0012-usb-stack.ja.md)のUSB対応に直結
4. **C-2のHSE**。ただしHSEは**板の属性**(水晶の有無と周波数)なので、
   chip側のデータで足りるのは「許容範囲」まで。板側は別問題
5. V407 / X315の多段ツリー(C-1)。表現の一般化が要るので後

## 依頼を出すまでのこちらの動き

データが揃うのを待つとPLL対応が止まるので、**当面はEVTから手で起こして進める**。
ただし[R-23](tinyusb-vendor-header.ja.md)と同じ扱いにする。

- 手写しの値は**FAMILY表に置き、出典をコメントで残す**
- **EVTの`SetSysClockTo*`から抽出した値と突き合わせる自動チェック**を付ける
- 上流のデータが来たら**そちらを正本に切り替える**
