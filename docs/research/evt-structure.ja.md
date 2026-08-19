# R-02: EVT構造とboardオプション軸

調査基準日: 2026-08-19
関連: [Q-001, Q-012, Q-013, Q-015](../open-questions.ja.md)

## 調査目的

WCH EVTの構造(linker script、clock初期化、デバイス選択マクロ、debug出力)を横断調査し、Arduino Board Managerの「ボードオプション(Toolsメニュー)」に何の軸を用意すべきかをEVTの実態から根拠付きで抽出する。

## 確認済み事実

### 1. EVTリポジトリの共通構造

全familyミラーは同じ構成を持つ。

```
<family>/
  datasheet_en/ datasheet_zh/   一次資料PDF
  documents.json                文書カタログ
  EVT/
    PUB/                        評価ボードマニュアル+回路図
    EXAM/
      SRC/                      ← 全exampleが共有する土台(コアの取込対象はここ)
        Core/    core_riscv.{c,h}  (QingKe共通機能: SysTickアクセス等)
        Debug/   debug.{c,h}       (delay、printf出力先)
        Ld/      Link.ld
        Peripheral/ inc/ src/      (SPLスタイルのペリフェラルdriver)
        Startup/ startup_*.S
      <カテゴリ>/<example>/User/   (main.c, ch32*_conf.h, ch32*_it.c, system_ch32*.c)
```

重要: **`system_ch32*.c`(clock初期化)はSRCではなく各exampleのUser/側にある**。つまりWCHは「clock設定はアプリケーションごとの選択」として扱っている。

### 2. EXAMカテゴリに現れるfamilyの機能差

board optionというよりコアの対応API範囲(Q-003)の資料になる。特徴的なもののみ:

| family | 特徴的カテゴリ |
|---|---|
| CH32V003/V006 | TOUCHKEY, OPA, SDI_Printf(全familyにあり) |
| CH32V103 | USB(device), RTC, BKP |
| CH32V20x | USB, CAN, BLE(D8W=V208), ETH(D8), OPA |
| CH32V205 | USBFS/USBHS, USBPD, CAN, QSPI, FSMC, PIOC, RunInRam_LP |
| CH32V307 | ETH, USB, CAN, DAC, DVP, FSMC, SDIO, RNG, I2S, FPU |
| CH32V407 | ETH, LTDC, I3C, PSRAM, ARGB, SDIO, USBHS |
| CH32X035 | USB, USBPD, PIOC, TOUCHKEY, OPA |
| CH32X315 | USBSS(SuperSpeed), USBHS, ARGB |
| CH32L103 | 低消費電力(LPTIM, RunInRam_LP), USB, USBPD |
| CH32M030 | モータ向け(OPA), USBPD |
| CH32H417 | dual core(CPU), SerDes, USBSS, HSADC, GPHA他多数 |

### 3. linker script

- 構造は全family同一テンプレート: `.init` → (`.vector`) → `.text` → ctors/dtors/init_array(定義のみ、startupは呼ばない) → `.data`(`__global_pointer$ = .+0x800`) → `.bss` → RAM末尾に`.stack`
- family差は実質**MEMORYのORIGIN/LENGTHとstack size**(V003=256, V00X=512, 他=2048)だけ
- **SKU差の扱い**: V00XのLink.ldは3構成(16K/4K, 32K/6K, 62K/8K)をコメント切替で併記。V20x/V307/V407は「FLASH+RAM分割の組合せ」をコメントで列挙(例: V307は192K+128K/224K+96K/256K+64K/288K+32K/128K+192K)し、代表値1つを有効化。**分割の実際の切替はoption byteによるもので、ldは合わせて書き換える前提**(コメントからの推測)
- 特殊: V407はRAM ORIGINが`0x20000000+1024`、LENGTHが`136K-1K`(先頭1KB予約。理由は要照合)。X315正本ldはFLASH 192K(datasheet上は480K。zero-wait領域との関係は要照合)
- 非正本の.ldは186個あるが、OS移植と特殊example(VectorInRAM/RunInRAM/IAP/BootAsUser/PIOC/Standby_RAM)用
- **VectorInRAMのld差分は`.vector`セクションを`>FLASH`から`>RAM AT>FLASH`へ変えるだけ**(V205で確認)

### 4. clock初期化(system_ch32*.c)

選択肢は`#define SYSCLK_FREQ_*`のコメント切替。**アクティブな1つがハードコードされており、`#ifndef`ガードがないため-Dでの外部上書きはできない**(V00Xで確認)。コアで扱うにはown実装または生成が必要。

| family | 選択肢(HSI系/HSE系) | EVTデフォルト |
|---|---|---|
| CH32V003 | 8/24/48MHz HSI、8/24/48MHz HSE | 48MHz HSE |
| CH32V006(V00X) | 同上 | **48MHz HSI** |
| CH32V103 | 48/56/72MHz 各HSI/HSE | 72MHz HSE |
| CH32V20x | 48〜144MHz 各HSI/HSE | 96MHz HSE |
| CH32V205 | 40〜192MHz 各HSI/HSE + HSI_LP | 160MHz HSE |
| CH32V307 | 48〜144MHz 各HSI/HSE | 96MHz HSE |
| CH32V407 | SYSCLKとHCLKのペア指定(120/240/350/400MHz、各HSI/HSE) | 400MHz(HCLK 200MHz) HSE |
| CH32X035 | 8/12/16/24/48MHz **HSIのみ(HSEなし)** | 48MHz HSI |
| CH32X315 | SYSCLK/CoreCLK/HCLKの組指定(240/312.5/480MHz、各HSI/HSE) | 240MHz HSE |
| CH32L103 | 48〜96MHz 各HSI/HSE + HSI_LP | 96MHz HSI |
| CH32M030 | 24/48/72MHz 各HSI/HSE | 72MHz HSI |
| CH32H417 | (system_ch32h417.cは選択defineなし、PLL表による構成。詳細未調査) | − |

HSI_VALUEはfamilyで異なる(V003/V00X=24MHz、X035=48MHz、V407/X315=20MHz、H417=25MHz、他=8MHz)。

### 5. デバイス選択マクロ(vendorヘッダ)

デバイスヘッダは`#if !defined(...)`のフォールバック付きで、**-D注入により無改変でライン切替できる**。

- `ch32v20x.h`: `CH32V20x_D6`(V203F/G/K/C) / `CH32V20x_D8`(V203RB) / `CH32V20x_D8W`(V208)、既定D6
- `ch32v30x.h`: `CH32V30x_D8`(V303) / `CH32V30x_D8C`(V307/V305/V317)、既定D8C
- `ch32v00X.h`: `CH32V002` / `CH32V004` / `CH32V005` / `CH32V006` / `CH32V007_M007`、既定CH32V006
- `HSE_VALUE`は主要familyで`#ifndef`ガード付き → **-Dで基板ごとの水晶値を上書き可能**。V20xはD8/D8W時に32MHz既定、`HSE_VALUE_12M`という専用スイッチも持つ
- family名とヘッダ名のねじれ: CH32V003→`ch32v00x.*`、CH32V006→`ch32v00X.*`(大文字X、V002〜M007の6 seriesを包含)、V407→`ch32v4x7.*`、X315→`ch32x3x5.*`、V307→`ch32v30x.*`(V303/305/307/317を包含)、V205→`ch32v205.*`(V203とは別物)

### 6. debug出力(Debug/debug.c)

- `#define DEBUG DEBUG_UART1`等でprintf出力先UARTを選択(V003/V00Xはremap先も選択肢: `DEBUG_UART1_NoRemap`等)
- 全familyに`SDI_Printf` example(WCH-Linkのdebugチャネル経由printf、UART不要)がある

### 7. 評価ボード(EVT/PUB)

- 1 familyにつき評価ボードマニュアル+回路図が1セット(V00Xのように1ボードで6 seriesを扱うものもある)。「EVTボード」をArduinoのboard定義にする場合も、実体はseries+packageの代表例にすぎない

## boardオプション軸の候補(提案)

EVTの実態から、メニュー軸は次のように分類できる。

| 軸 | 実装先 | 必須/任意 | 備考 |
|---|---|---|---|
| **series/SKU** | -D(ライン選択マクロ)+ variant + ld(FLASH/RAM値) | 必須 | ボード定義そのものかpnumメニュー(R-03参照) |
| **clockプロファイル** | own system実装への-D(EVTの選択肢を踏襲) | 必須 | 既定はHSI系が安全(裸チップにHSEはない)。EVTデフォルトがHSE系のfamilyは要注意 |
| **HSE値** | `-DHSE_VALUE=` | 任意 | ヘッダが`#ifndef`対応済み。clockプロファイルとの整合検証が必要 |
| **FLASH/RAM分割** | ld選択(+option byte書込み) | V20x/V307/V407のみ | option byte変更を書込み手順に含めるかが論点 |
| **VectorInRAM** | ld選択 | 任意 | startup正規化後はld差分のみ(R-01) |
| **debug printf出力先** | -D(UART番号/remap/SDI/なし) | 任意 | Arduino的には`Serial`設計と関係。SDI printfはUARTを消費しない利点 |
| **stack/heapサイズ** | ld変数(`__stack_size`) | 任意 | EVT既定は256/512/2048 |
| **最適化/C++等のビルド設定** | platform.txt/menu | 任意 | EVT由来ではないが慣例的メニュー |

**メニューにしない方が良いもの**(提案): highcode有無(sketchが使わなければサイズゼロなので常時有効化できる見込み)、ライン選択とseries選択の二重化(SKU選択から一意に導出できる)。

## 判断ポイント

- clock初期化をvendor `system_*.c`のpatch運用にするか、own実装(またはdevice-data生成)へ置き換えるか。**-D上書き不可というvendor実装の制約から、own実装が有力**(Q-013)
- FLASH/RAM分割(V20x/V307/V407)を初期リリースの対象にするか。option byteの扱い(uploaderの責務)が絡むため、初期は既定分割のみに限定する案が現実的
- Arduinoの`Serial`をどのUART/SDIに割り当てるかと、debug printfメニューの関係整理
- EVTデフォルトclock(HSE系が多い)とArduinoボード既定(裸チップ前提ならHSI)の方針統一
- X315のld FLASH長(192K vs datasheet 480K)とV407のRAM先頭1KB予約の理由確認(reference manual照合)

## 未検証事項

- FLASH/RAM分割の切替手順(option byte)と、分割変更後のld整合の実機確認
- H417のclock構成方法の詳細(選択defineがない)
- `ch32*_conf.h`のexample間差分(全数比較は未実施。少なくともmodule有効化の集合がexampleごとに異なる)
- SDI printfのhost側受信方法(WCH-Link/probe-rsでの実用性)
