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

`ANY` の容量は安全側（最小）、pin と周辺機能は最大（全 part の和集合）です。

| 項目 | `ANY` | part を選んだとき |
|---|---|---|
| flash / RAM、linker script | series 内の最小 | その part の値 |
| 使える pad（`digitalPinIsValid()`、`CH32RV_PORT_MASK_*`） | 全 part の和集合 | その package に bond されている pad |
| 周辺機能の instance | すべてある | bond されていない instance は `CH32RV_<名前>_ABSENT` で無くなる |
| route の表（`setRoute()` / `setPins()`、board menu の FIX） | 全 part の和集合 | その part の表（pad が package で動く場合） |
| 既定の pin（pin なしの `begin()`） | なるべく全 part にある pad。debug / strap pad は使わない | 同じ規則で、その part にある pad |

`ANY` で書いたスケッチが、実際の part に無い pad を使うことは止めません。どの part でも動くことを確かめたい
ときは `digitalPinIsCommon()`（`CH32RV_PORT_COMMON_MASK_*`、全 part に bond されている pad）で判定します。
これは参考の判定で、`ANY` で使える pad を制限するものではありません。

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
