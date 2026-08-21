# ADR-0013: 同梱ライブラリは3つの基準を満たすものだけにし、examplesはライブラリで配る

- Status: Accepted(2026-08-21、maintainerが明示承認)
- Date: 2026-08-21
- Related questions: Q-011、[R-25](../research/core-scope.ja.md)、[ADR-0009](0009-arduinocore-api-import.ja.md)、[ADR-0012](0012-usb-stack.ja.md)

## Context

`SerialSDI`をcoreに置いたことをきっかけに、「coreの範囲は何で決まるのか」を整理した
([R-25](../research/core-scope.ja.md))。同時に、同梱ライブラリが増え始めている
(`Wire` / `SPI` / `Servo` / `TinyUSB` / `SerialSDI`)ので、
**何を同梱し、何を同梱しないか**の基準が要る。

Arduinoには制約がもう1つある。**プラットフォームはライブラリ経由でしかexamplesを配れない**。
coreのAPI(`digitalWrite`、`tone`…)のexamplesにも、置き場所としてライブラリが要る。

## Decision

### 1. 同梱するライブラリは次の3つのどれかに当てはまるものだけ

| 基準 | 該当 |
|---|---|
| **Arduinoの事実上の標準API**を提供する | `Wire`、`SPI`、(将来)`EEPROM` |
| **coreの機能を出すのに必要** | `TinyUSB`(USBスタック)、`SerialSDI`(このチップ固有の出力経路) |
| **ハードウェア調停がcore側に必要**で、外部ライブラリでは書けない | `Servo`(timerの排他をgeneratorが決めている) |

**どれにも当てはまらないものは同梱しない。** センサやディスプレイのドライバは外部で足りる。

### 2. coreに置くのは次のいずれかだけ

1. ArduinoCore-APIが宣言しているもの(sketchが`#include`なしに呼べる)
2. どのsketchもリンクに必要なもの(crt0、`main()`、syscalls、`_sbrk`、C++サポート)
3. 同梱ライブラリが土台にするHAL契約(`ch32_registers.h`、`ch32_gpio.h`、
   `ch32_pins.h`、`ch32_route.h`、variantの`pins_arduino.h`)

どれでもないものはライブラリへ出す。この基準で`SerialSDI`を`libraries/`へ移した。

### 3. examplesはライブラリに置き、coreのAPIのぶんは`libraries/CH32/`へ

`libraries/CH32/`は「レジスタレベルの逃げ道(`CH32.h`)」と
「coreのAPIのexamples」を兼ねる。ESP32が同じ問題に対して
IDEの警告回避のためだけの`src/dummy.h`を置いているのに対し、
**こちらは同じファイルに実際の役目を持たせた**。

規約は[R-25](../research/core-scope.ja.md)に書いた。要点は
「pinはvariantの名前から取り数値リテラルを書かない」
「examplesはテストではない」「V003に載ること」
「`README.md`と`README.ja.md`を付ける」「CIで全examplesをコンパイルする」。

## Consequences

- 同梱ライブラリは**それ自体がAPIの説明責任を持つ**。
  `examples/`・`README.md`/`README.ja.md`・`keywords.txt`が揃って1セット
- 同梱を断る基準ができたので、「便利だから入れる」で膨らまない
- `libraries/CH32/`は性質が2つある(逃げ道 + examples)。
  これは折衷であって美しくはないが、Arduinoの配布の仕組みがそう強いている
- coreの3基準は`SerialSDI`以外にも将来効く。
  たとえばUSB CDCを`Serial`にする話は「coreがCDCを知る」ことになるので、
  基準2(全sketchが必要とするか)に照らして設計する必要がある

## Alternatives considered

| 案 | なぜ採らないか |
|---|---|
| 同梱の基準を設けない | ライブラリは必ず増える。増えたぶんだけrelease archiveと保守が重くなる |
| examplesをdocsだけで配る | IDEのExamplesメニューに出ない。Arduinoの利用者はそこから始める |
| coreのAPIのexamplesを各ライブラリへ散らす | `digitalWrite`のexampleを`Wire`に置くことになる。対応が取れない |
| `libraries/CH32/`を純粋なexamples置き場にし、`dummy.h`を置く(ESP32方式) | 同じファイルを置くなら、逃げ道の入口という役目を持たせたほうが良い |
