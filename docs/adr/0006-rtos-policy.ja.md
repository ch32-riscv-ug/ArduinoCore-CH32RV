# ADR-0006: コアはベアメタル単一セマンティクスとし、RTOSは将来の同梱ライブラリで提供する

- Status: Proposed
- Date: 2026-08-19
- Related questions: Q-006, Q-003, Q-013

## Context

EVTはV103/V20x/V205/V307/X035/L103/M030にFreeRTOS/RT-Thread/TencentOS/LiteOS-mの移植例を持つが、小容量family(V003/V00X)には提供していない。対象SKUはRAM 2KB〜であり、Arduinoコア層でRTOSをどう扱うかはコアのセマンティクスとboardメニュー構成に直結する。

## Decision drivers

- RAM 2KB(CH32V003)〜128KB超まで単一コアで扱う
- 外部OSへの常時依存リスク(ArduinoCore-mbedはMbed OS EOLでZephyrへ全面書き直し)
- 前例の実証済みの弱点を避ける(ESP32: config変更不可、arduino-pico: Config固定、メニュー化のCI二重化コスト)
- 調査: [R-16](../research/rtos-support.ja.md)

## Options considered

### コアをRTOS上に構築(ESP32型)

V003(2KB)で成立せず、セマンティクスが2系統化。外部OS EOLリスク。不採用。

### RTOS有無をメニュー化(arduino-pico型)

コアに`#ifdef`二系統が生じ、CI/保守が実質2コア分。初期には過剰。将来の限定的採用余地のみ残す。

### ライブラリとして提供(UNO R4/STM32duino型)(採用)

未使用時コストゼロ、config柔軟性をスケッチ側置換で担保できる実証済みの形。

## Decision

- **コアは全familyでベアメタル単一セマンティクス**とし、RTOS上に構築しない(大容量familyも同様)
- **初期リリースはRTOSなし**
- 需要が確認できたら「**コア同梱FreeRTOSライブラリ(include一発、メニューなし。UNO R4方式)**」を第一候補として追加する。コアAPI統合(delay/yieldのscheduler協調)が必要になった場合のみ、`-D`注入メニューを**RTOS対象familyに限定して**追加する(V003/V00X系boardにはメニューを出さない)
- カーネルはEVT改変版を固定化せず**FreeRTOS公式(V10.5.0+でRV32E対応)ベース**とし、PFIC/CSR差分をport層へ局所化する。EVT移植は挙動の参照資料
- コア設計に以下のフックを最初から織り込む(Q-013の要件):
  - millis/delayの**tickソースを差し替え可能**にする(RTOS時はtick hookから供給)
  - weakハンドラ設計によりライブラリがSysTick/SW handlerを占有できる状態を維持する(ADR-0003で成立済み)
  - RTOS用startup差分(INTSYSCR/mstatus初期値)はcrt0の`-D`注入軸で表現する(ADR-0003で成立済み)
  - 同梱ライブラリはプリコンパイル配布にしない(config変更を殺すため)

## Consequences

- 初期リリースのメニュー・FQBN・CI matrixへの影響ゼロ
- RTOS利用者向けの提供は需要待ちになる(それまでは各自ライブラリ導入)
- RT-Thread等の他RTOSは独立ライブラリとしてコミュニティに開放する形を想定
- H417 dual core(V5F+V3F)のOS/AMP構成は別論点として保留

## Validation

- コアAPI実装レビューで「schedulerの存在を仮定しない/ tickソースが差し替え可能」を確認する
- RTOSライブラリ追加時: 代表board(RTOS対象family)のみのOS別compileをCIへ追加(全数二重化しない)

## References

- [R-16調査](../research/rtos-support.ja.md)(EVT実態、前例比較、URL付き)
