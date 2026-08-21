# R-22: USB device/hostに何を使うか(調査)

日付: 2026-08-21
状態: **調査のみ。採用は未決**
関連: [ペリフェラル対応表](../peripheral-support.ja.md)、[R-20](register-map-data.ja.md)、[todo](../todo.ja.md)

## 問い

USB device/hostは**TinyUSBを使うのか、ベンダ独自APIを叩くのか**。

## 結論(先に)

- **WCHの既存物はすべて独自API**。公式Arduinoコアに至ってはUSBスタックを持っておらず、
  EVTの例をライブラリとして同梱しているだけ
- **TinyUSBにはCH32の正式ドライバがある**(MIT)。device(FS/HS)に加え**host(FS)もある**。
  ただし対応は**V103 / V20x / V30x / F20x**で、**X033/X035はPR #3703が未マージ**、
  L103/M030/V205/V407/X3x5は**対応なし**
- 我々にとっての本当の障害はスタック選定ではなく**クロック**。
  X035以外はUSBに48MHzが要り、それは**PLL**から来る。今のコアはHSI直結でPLLを持たない

## 既存の3つのやり方

### 1. WCH EVT(独自API)

familyごとに`EXAM/USB/`以下へ、IP別のディレクトリと例が入っている。

| series | EVTのUSBディレクトリ |
|---|---|
| V103 | `USB` |
| V203/V208 | `USB/USBD`(ST FSDEV系) + `USB/USBFS` |
| V303/305/307/317 | `USB/USBFS` + `USB/USBHS` |
| V205 | `USBFS` + `USBHS` |
| V407/V467 | `USBHS` |
| L103/M103 | `USB` |
| M030 | `USB` |
| X033/X035 | `USB/USBFS`(DEVICE + HOST_KM/Udisk/MTP/IAP) |
| X305/X315 | `USBHS` + `USBSS` |

**hostのファイルシステム層は`libRV3UFI.a`というバイナリ**でしか提供されていない
(X035・V307とも)。つまり「WCHのhostスタックを使う」は、
EVTを取り込まない我々の方針以前に**ソースが無い**。

### 2. WCH公式Arduinoコア

`libraries/EVT/examples/<series>/USB/...`にEVTの例を置いてあるだけで、
**coreとしてのUSBスタックは無い**。`Serial`はUARTのみ。

### 3. TinyUSB(MIT)

`src/portable/wch/`に**専用ドライバ**がある。

| ファイル | 役割 |
|---|---|
| `dcd_ch32_usbfs.c` | device、full speed |
| `dcd_ch32_usbhs.c` | device、high speed |
| `hcd_ch32_usbfs.c` | **host**、full speed |
| `ch32_usbfs_reg.h` / `ch32_usbhs_reg.h` | レジスタ定義(自前) |

ドライバが読むのは`tusb_option.h`と自分のレジスタヘッダだけで、
**ベンダSDKを要求しない**。BSPは`ch32v10x` / `ch32v20x` / `ch32v30x` / `ch32f20x`。

`tusb_mcu.h`の対応は以下。

| MCU定義 | device | host | 備考 |
|---|---|---|---|
| `OPT_MCU_CH32V103` | ○(USBFS) | - | |
| `OPT_MCU_CH32V20X` | ○(port0=**ST FSDEV**、port1=USBFS) | **○**(USBFS) | V20xのUSBDはSTのIPクローン |
| `OPT_MCU_CH32V307` | ○(FS+HS、既定HS) | - | |
| `OPT_MCU_CH32F20X` | ○(FS+HS) | - | 我々の対象外 |

## 我々のseriesとの対応

| series | USB IP | TinyUSB | USBに48MHzをどう作るか |
|---|---|---|---|
| V103 | USBD | ○ | **PLLが要る**(HSI 8MHz) |
| V203/V208 | USBD + USBFS | ○(host可) | **PLLが要る**(HSI 8MHz) |
| V303/305/307/317 | USBFS + USBHS | ○ | **PLLが要る**(HSI 8MHz) |
| V205 | USBFS + USBHS | ✕ | PLL |
| V407/V467 | USBHS | ✕ | PLL |
| L103/M103 | USB | ✕ | PLL |
| M030 | USB | ✕ | PLL |
| **X033/X035** | USBFS | **△ PR #3703が未マージ** | **不要**。HSIが48MHzで、EVTの初期化も`RCC_AHBPeriph_USBFS`を有効にするだけ |
| X305/X315 | USBHS + USBSS | ✕ | PLL |
| V003/V00x | 無し | - | - |

EVTのV20x側は`RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div2)`のようにPLLから分周している。
**現在のコアはHSI直結でPLLを実装していない**([todo](../todo.ja.md)の`[P1]`)ので、
X035以外はUSB以前にそこで止まる。

### PR #3703(CH32X035)の状態

- **open、未マージ**(最終活動 2026-07-21)
- 著者 rhgndf、maintainerのhathach/HiFiPhileが対応中
- 内容: X035/X033のdevice対応。EP4にハードウェアの自動トグルが無い点と、
  TX/RX制御バイトが1つにまとまっている点への対処
- **マージの障害は「HILで試せていないこと」とされている**

我々は**X035の実機を持っている**。ここは上流に返せる数少ない貢献の1つ。

## USB PDは別の話

同じ`USB`でもPDはまったく別のペリフェラルで、**TinyUSBの守備範囲外**。

- `EXAM/USBPD/USBPD_SNK`と`USBPD_SRC`は**完全なソース**(`PD_Process.c`が約824行)。
  バイナリを使っているのは`USBPD_CH211`という特定用途の例だけ
- したがって**PDは自前実装が可能**。必要なのはPDペリフェラルのレジスタ定義(R-20のD-3)

## 選択肢

| 案 | 中身 | 良い点 | 悪い点 |
|---|---|---|---|
| **A. TinyUSBを同梱** | `libraries/USB/`等としてTinyUSBを持ち、coreはクロックと割込みだけ用意 | MITで再配布可。host込み。arduino-pico等と同じ選択でエコシステムに乗る | 対応が3 seriesのみ。X035はPR待ち。sketchのflashが+8〜12KB |
| **B. 自前スタック** | device CDCだけ自分で書く | 小さくできる。全familyを自分の裁量で足せる | descriptor管理・列挙・EP0の面倒を全部抱える。**車輪の再発明** |
| **C. 当面やらない** | USBはM2以降 | 今はPLLが無いのでX035以外は動かせない | 「USB CDCで書き込み後そのままSerial」が当分できない |

## 決めていないこと

- [ ] `[要判断]` A / B / C
- [ ] `[要判断]` Aなら**同梱の形**: coreに埋めるか、同梱ライブラリか、Board Manager外の依存にするか
- [ ] `[要判断]` **USB CDCを`Serial`にするか**。ESP32-C3等は既定でそうなっている。
      やるならFQBNメニュー(`USB CDC: Enabled/Disabled`)が要る
- [ ] `[P1]` いずれにせよ**PLL対応が先**(X035を除く)。これは既にtodoに`[P1]`である
- [ ] `[P2]` 上流貢献: **PR #3703をX035実機で試して結果を返す**。
      マージの障害がHIL不足なので、実機を持っている側の価値が高い
