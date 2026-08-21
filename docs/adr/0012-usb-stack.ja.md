# ADR-0012: USBはTinyUSBを採用し、未対応seriesは上流へ載せにいく

- Status: Accepted(2026-08-21、maintainerが明示指示)
- Date: 2026-08-21
- Related questions: Q-011、[R-22](../research/usb-stack.ja.md)、[ADR-0006](0006-rtos-policy.ja.md)

## Context

USB device/hostに何を使うかを決める必要がありました。調査は[R-22](../research/usb-stack.ja.md)。
判断材料は次の4点です。

1. **WCHの既存物はすべて独自API**で、しかも**公式Arduinoコアには
   USBスタックが無い**。EVTの例をライブラリとして置いてあるだけ
2. **TinyUSB(MIT)にはCH32の正式ドライバがある**。`src/portable/wch/`に
   device(FS/HS)と**host(FS)**があり、ドライバが要求するのは`tusb_option.h`と
   自前のレジスタヘッダだけで、**ベンダSDKに依存しない**
3. WCHのUSB **hostのファイルシステム層は`libRV3UFI.a`というバイナリでしか
   提供されていない**。EVTを取り込まない方針以前に、ソースが存在しない
4. TinyUSBの現在の対応は**V103 / V20x / V30x**にとどまり、
   **X033/X035はPR #3703が未マージ**、L103/M103・M030・V205・V407/V467・X305/X315は未対応

## Decision

**USB device/hostのスタックはTinyUSBとする。** 自前のUSBスタックは書かない。

**未対応のseriesは、privateに抱えるのではなく上流(hathach/tinyusb)へ載せにいく方針とする。**
我々がどうしても必要な期間だけ手元にpatchを持つことはあっても、
**恒久的なforkは作らない**。

## Consequences

- 我々が書くのは**クロック・割込み・pinの用意とArduino側の見せ方**だけになる。
  descriptor管理や列挙といったUSBの本体は持たない
- **hostが視野に入る**。WCH側にソースが無い以上、hostをやるならこの道しかない
- **順序としてPLLが先**。EVTのV20xは`RCC_USBCLKSource_PLLCLK_Div2`のように
  PLLから48MHzを作っており、現在のコアはHSI直結でPLLを持たない。
  例外は**X033/X035**で、HSIが48MHzのためそのまま動く
- 上流へ載せる作業には**実機が要る**。PR #3703が止まっている理由がまさにそれで、
  X035実機を持っているこちらの立場は貢献として意味がある
- TinyUSBはMITなので**同梱に制約は無い**。同梱の形(coreに埋めるか、
  同梱ライブラリか)は別途決める

## Alternatives considered

| 案 | なぜ採らないか |
|---|---|
| 自前のdevice CDCスタック | descriptor・列挙・EP0を全部抱える割に、得るものが無い。hostは事実上不可能 |
| WCHのEVTスタックを移植 | familyごとに別API。**hostはバイナリのみ**。EVT非取り込み方針とも衝突 |
| USBを初回releaseの対象外にする | 「書き込んだらそのままSerial monitor」が当分できない。X035は今すぐ動かせる位置にいる |
| TinyUSBをforkして全family対応 | 短期は速いが、上流の変更に追随し続ける負債になる。**上流へ返す方が総量として安い** |
