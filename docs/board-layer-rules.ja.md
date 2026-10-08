# Board、series、part、sketch の責務

## Series

Generic board は silicon series を表します。次は series / variant の責務です。

- 利用可能な pad と peripheral route
- pin capability、ADC channel、timer channel、IRQ
- HSI または HSI 由来 PLL を使う既定 clock 設定
- Arduino peripheral の既定 pin
- timer 等の内部 resource の既定割当

Generic board は実在する基板を表さないため、`LED_BUILTIN` や水晶、外付け部品を仮定しません。

## Part number

`pnum` menu は exact part の次だけを選びます。

- flash / RAM size と linker script
- package と、その package に bond されている pad
- part 固有の vector / die variant

`ANY` は series 内の最小 flash / RAM を使います。基板の pin 配置や機能を選ぶ menu ではありません。

## 製品 board

製品 board は series の定義を再利用し、基板上で確定するものだけを追加します。

- シルク印刷された pin 番号から pad への表
- LED、button、connector 等の名前
- 外部 clock source
- bootloader と upload protocol
- 基板 revision に依存する配線

製品 board の ID には基板 revision を含めます。revision 間で pin 配置が変わっても既存 FQBN の意味を変えないためです。

## Sketch と library

Generic board 向けの code は `PA1`、`SDA`、`SCK` 等の pad / role 名を使い、整数 pin を直書きしません。
Generic board の 0〜63 は無効です。製品 board だけがシルク番号をこの範囲へ割り当てられます。

example が特定の配線を必要とする場合は、sketch 冒頭で pad と外部部品を明記します。example の便宜で generic
variant に LED 名や connector 名を追加しません。

## 生成

Generic board、part menu、variant、linker script は device-data から生成します。製品 board の基板固有情報だけを
この repository で管理します。この境界により、silicon の事実と個別基板の都合を混ぜません。

関連する判断は [ADR-0005](adr/0005-board-structure-and-fqbn.ja.md) と
[ADR-0010](adr/0010-pin-numbering.ja.md) を参照してください。
