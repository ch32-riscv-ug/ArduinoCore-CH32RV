# boardレイヤの定義権限

- 状態: **提案**(ADR未起票)
- 文書基準日: 2026-08-29
- 関連: [ADR-0005](adr/0005-board-structure-and-fqbn.ja.md)(board=1 series)、
  [ADR-0010](adr/0010-pin-numbering.ja.md)(pin番号)、
  [examples-build-rules.ja.md](examples-build-rules.ja.md)(§4-1の後続)、
  [todo.ja.md](todo.ja.md):919(LED_BUILTIN placeholder)、同:198/732(HSEは板の属性)

## 0. 前提

**リリース前で実ユーザがいない。破壊的変更をしてよい時期。**
移行パスや非推奨期間の設計に費用を払わず、正しい形へ直接動かす。

## 1. 問題

いま存在するboardはGeneric(=1 silicon series)だけで、`menu.pnum`で型番を選んでも
決まるのはflash/RAMとprobe-rs chipに限られる。pin系は**実ボードの配線**で決まるため、
Genericでは原理的に確定しない。

にもかかわらずvariantは`LED_BUILTIN`のような「板の配線の主張」を出している。
**どの層が何を定義してよいのかの規則が無い**のが問題であって、個々のpin選定が悪いわけではない。

## 2. 層

| 層 | 実体 | 決まるもの |
|---|---|---|
| L0 silicon series | `variants/<SERIES>/pins_arduino.h`(生成) | pad名、port mask、ADCマップ、route表、PWM/timerマップ、clock enable、IRQ、vector table |
| L1 SKU (型番・package) | `menu.pnum` | flash/RAM、ldscript、probe-rs chip、**bondされているpadの集合** |
| L2 board (基板) | 製品boardのvariant(未実装) | on-board LED/ボタン、HSE有無と周波数、USB配線、header露出pad、silkscreen名、**既に何かに繋がっていて触ってはいけないpad** |
| L3 sketch | ユーザコード | 上記以外すべて。`setPins()`/`setRoute()`/コンストラクタ引数 |

## 3. 判定基準

> **variantが名前を定義してよいのは、その値が silicon (L0 × L1) から導けるときだけ。**
> **板の配線でしか決まらない名前は、Generic boardでは定義しない。**

| 名前 | 分類 | 扱い |
|---|---|---|
| `PA0`…、`A0`…、`CH32_*_ROUTES`、PWM/timerマップ、`CH32_CLKEN_*` | L0の事実(座標) | 変更なし |
| `SDA/SCL`、`SCK/MISO/MOSI`、`SS`、`SERIAL1_TX/RX`、`CH32_SERIAL_DEFAULT` | L0のシリコン既定route | **残す**。文言のみ修正 |
| `CH32_PORT_MASK_*` | L1で決まるべき | **SKU別に生成する**(現状L0止まり) |
| `LED_BUILTIN` | L2の主張 | **Genericでは定義しない** |
| HSE有無/周波数、USB配線、ボタン | L2 | データが無い。L2の受け皿ができるまで保留 |

**系** — この基準は「examplesの都合で名前を作ってよい」を含まない。
examplesが要る値はexamplesが持つ(§4-1)。

### 「SPIのデフォルトpinは無いほうが安全では」への回答

`PIN_SPI_SCK`等はデータシートの既定routeのpad、`PIN_SPI_SS`は選ばれたrouteのNSS pad
(`tools/generate/generate.py:1923`)。**板の主張ではなくシリコンの事実**なので基準上は残る。
生成器は既にdebug/strap padを既定から除外している(`load_forbidden_pads`, 同:1092)。

危険なのは名前の存在ではなく、**スケッチが名前を書いていないpadを`begin()`が黙って駆動すること**。
名前は定数であって、あるだけでは何も駆動しない。ここはArduinoライブラリ互換との
トレードオフなので名前は残し、「Genericでの既定は*板のピンではなくチップの既定route*である」と
文書で明示する。既存方針(明示指定が主、既定が要るのはSerialくらい)と整合する。

生成コメント `/* Arduino's standard names for the first bus (SPI). */` は誤読を招くので
「SPI1の既定routeのpad。板の配線ではない」の意味に書き換える。

## 4. 決定

### 4-1. `LED_BUILTIN` をGenericから外す。代替名は作らない (**実装済み**)

現状 `generate.py:2060` は `led = common[0]`、つまり**共通padの最小番号**を取るだけ。
周辺routeの選定と違い `load_forbidden_pads` を通っていない。
いま24 variant全部がPA0/PA1/PA2に落ちて実害が無いのは偶然。

**variantに例示用padの名前を作ってはいけない**(2026-08-29決定)。
`CH32_COMMON_PIN_n` のような名前をvariantに置くと、プラットフォームが認めたpinに見えるのに
中身は「たまたま全型番にあった一番若いpad」で、利用者から見て意味のない番号になる。
`LED_BUILTIN`の問題を名前を変えて温存するだけで、混乱を増やす。

**examplesはCIの代表ボードでしか検証しない。** 現状の
`tests/compile/compile_examples.py:33` は `BOARDS = ("CH32X035", "CH32V003")` を
`pnum=ANY` でビルドしている(全24 seriesではない)。この検証範囲そのものは
[examples-build-rules.ja.md](examples-build-rules.ja.md)で作り直すが、
「examplesは全seriesで通る必要がない」という前提は変わらない。

**pad名は全24 seriesで2本しかない**(実測、当初の見積りを訂正):

```
全24 seriesに名前が存在するpad : PA1  PA2      (2本のみ)
うちPA1がPWM可                 : 20 / 24 series
```

当初は代表2ボード(X035/V003)の積である10本(`PA1 PA2 PC0..PC7`)を前提にしたが、
[examples-build-rules](examples-build-rules.ja.md)でsweepを全24 seriesへ広げたため、
`PC0`系は使えない(L103 / M103 / X033 に存在しない)。

結果、examplesのpadは:

- 1本要るもの → `PA1`
- 2本要るもの → `PA1` / `PA2`
- **3本要るのはShiftOutだけ**。3本目は `#if defined(PA4)` → `PC4`(V002/V003用)の
  2段フォールバックで全24 seriesを覆う。**`#if`を持つexampleはこれ1つ**

`analogWrite()`は非PWM padでdigitalWriteに落ちるので、PWMの有無はコンパイル制約に
ならない(M030/V205/X305/X315ではfadeしないが、ビルドは通る)。

| sketch | 現状 | 変更後 |
|---|---|---|
| CH32/Blink | `LED_BUILTIN` | `#ifndef LED_BUILTIN` → `#error` でbuild-property指定を案内 |
| CH32/Fade | `LED = LED_BUILTIN` | `PA1`(PWM pad) |
| CH32/AnalogResolution | `analogWrite(LED_BUILTIN,…)` | `PWM_PIN = PA1` |
| CH32/ShiftOut | `LED_BUILTIN` ×3(同一pin) | `PA1`/`PA2`/`PA4`(無ければ`PC4`) |
| CH32/PulseIn | `LED_BUILTIN` ×2(同一pin) | `PA1`/`PA2` |
| CH32/ToneMelody | `BUZZER = LED_BUILTIN` | `PA1` |
| CH32/PinInterrupt | `BUTTON = LED_BUILTIN` | `PA1` |
| Servo/Sweep | `SERVO_PIN = LED_BUILTIN` | `PA1` |
| CH32/PinCapabilities | `describe(LED_BUILTIN,…)` | `#ifdef LED_BUILTIN` で囲う |
| SerialRTT/RttEcho | LED点滅 | `#ifdef LED_BUILTIN` で囲う |

**Blinkだけ`#error`にする理由**: 他のexampleは「どれか1本要る」だけだが、
Blinkは**板のLEDが光ること**が主題なので、LEDの無いpadを黙って叩くと
「ボードが死んでいる」ように見える。ここは止めるほうが親切。

CIは`tests/compile/compile_examples.py`の`EXTRA_PROPERTIES`から
`build.extra_flags=-DLED_BUILTIN=PA1`を渡してBlinkをビルドする。
**利用者に案内しているのと同じ経路をCIが通る**ので、案内が壊れたらCIが落ちる。
これはスキップではない(exampleはビルドされる)。

いずれも先頭で `static const uint8_t X = PC0;` と宣言し、
「自分の配線に合わせて変えること」をコメントで言う。**PINは変更前提**。

ShiftOut/PulseInが同じpinを3回/2回使っているのは、現状のほうがバグに近い。

**テスト側への波及(要確認)**: `LED_BUILTIN`はexamplesだけでなく、
compile matrixのsmokeスケッチと実機self-testも使っていた。置き換えは:

| 対象 | ビルド範囲 | 変更後 | 理由 |
|---|---|---|---|
| `tests/compile/compile_matrix.py`の`BLINK` | 全122 SKU | `CH32_PIN(0, 0)`(PA0) | **全24 seriesに共通するpad名が無い**ため、pad名ではなく符号化値を直接書く。compile専用で実行されない |
| `core_api` / `servo_selftest` / `tone_selftest` | Tier A/B 6枚 | `PA1` | 6枚すべてにbondされ、かつ6枚すべてでPWM可能な**唯一のpad**(実測。もう1つの共通padはPA2だがPWM不可) |

**これは実機が駆動するpadの変更**である。従来は`LED_BUILTIN`の値、つまり
V003=PA1 / X035=PA2 / V203・L103・V103・V307=PA0 を叩いていた。
PA1へ統一したので、**fixtureの配線と衝突しないかは実機で確認が要る**。

**検証対象外のseriesでの挙動**: 利用者が別のseriesでexampleをビルドすると、
そのseriesにそのpadが無ければ「`PC0` が未宣言」というコンパイルエラーになる。
黙って別のpadを叩くよりよい。`#if defined(PC0)` のフォールバック連鎖を
examples側に書く案もあるが、ビギナー向けexampleの冒頭が汚れるので採らない。

### 4-2. pnumでpadを絞る (**実装済み**)

`digitalPinIsValid` は `CH32_PORT_MASK`(series union)を見る(`cores/arduino/Arduino.h:26`)。
`CH32V003J4M6`(SOP8, GPIO 6本)を選んでも `digitalPinIsValid(PC7)` は真になる。
`-DARDUINO_<part>` は `platform.txt:28` で既に出ているが、**読むコードが1つも無い**。

**実装**: `CH32_PORT_MASK_*` を `#if defined(ARDUINO_<part>)` チェーンでSKU別に生成し、
どれにも当たらないとき(=ANY)はseries unionにフォールバックする。
`CH32_PORT_COMMON_MASK_*` はANYの約束なので現状のまま。

- 規模: 122 pnumエントリ / 24 series、最大はV203の13(ANY込み)
- flashコスト: 定数畳み込みなのでゼロ
- 挙動変化: `tone()`/`Servo`が今まで受け付けていたpinを実行時に拒否する。
  これが狙い。リリース前なので移行への配慮は不要

### 4-3. L2(board)の受け皿は既に空いている

生成ヘッダの `LED_BUILTIN` / `PIN_WIRE_*` / `PIN_SPI_*` / `CH32_SERIAL_DEFAULT` は
すべて `#ifndef` ガード付き。**製品boardのvariantは「板の事実を先に`#define`してから
生成ヘッダを`#include`」するだけで成立する。**
この`#ifndef`は偶然ではなく**board overrideの継ぎ目**であることを文書化する。

L2が持つべきもの: on-board LED/ボタン、HSE有無と周波数、既定Serial instance/route、
silkscreen名エイリアス、板固有のforbidden pad。

## 5. 付随: レジスタの公開範囲

**現状は既に明示include方式になっている**(確認済み事実)。

- `Arduino.h` が includeするのは `ch32_pins.h`(25行: pin encoding、
  `CH32_GPIO_PORT_BASE`、`NOT_A_PIN`)と variant の `pins_arduino.h` のみ
- `ch32_registers.h`(473行のレジスタマップ)は `Arduino.h` から**到達しない**。
  `#include <CH32.h>` が唯一の入口
- `ch32_pins.h` が `CH32_GPIO_PORT_BASE` をレジスタマップと二重に持っているのは
  この分離のためで、ズレは `cores/arduino/wiring_digital.c:10-14` の
  `_Static_assert` が防いでいる
- `CH32.h` の冒頭に「この層は不安定」「コアも同じレジスタを使っているので
  `begin()`と喧嘩する」の2つの警告が既にある

**残る論点は別のところ**: variantの `pins_arduino.h` は `Arduino.h` 経由で全スケッチに入るが、
そこに `CH32_CLKEN_ADC1_ADDR 0x40021018u` のような**生アドレスが並んでいる**
(V003で137 define、V307で343 define)。レジスタマップは閉じているのに
variant側から周辺アドレスが漏れている形で、方針としては非対称。

- 実害: 名前空間だけ(全部マクロなのでコードサイズはゼロ)
- 選択肢: このままでよい / `CH32_CLKEN_*` 等をレジスタ系ヘッダへ寄せて
  variantからは `CH32.h` 経由でのみ見えるようにする
- ただし`CH32_CLKEN_*`は[examples-build-rules.ja.md](examples-build-rules.ja.md)で
  **capability tokenとして使う**ことにしたので、隠すなら別の参照経路が要る
- **要判断**

## 6. 未解決

- `SS` の位置づけ。NSS padという点でシリコンの事実だが、コアはSSM/SSIを使うので
  `SS`が名指すpadを**コアは駆動しない**。Arduino慣習の「板の既定CS」とも意味が違う
- §5のvariantからの生アドレス漏れ
- L2 variantの置き場所とboards.txtの書き方(生成対象にするか手書きにするか)

## 7. 参考: 実測メモ

`LED_BUILTIN`除去の検討中に取った測定。決定の根拠ではなくなったが記録しておく。

- **全24 seriesに共通するpadは存在しない**。V002/V003はPA0を持たず(最小PA1)、
  V305は最小PA1、X035/M030は最小PA2
- common padの数は V002の5個 から X305の53個まで
- common padには**debug/strap padが混ざる**: V002のPD1(SWDIO)、V203/V305のPA13/PA14
  (SWDIO/SWCLK)、X035のPC16/PC17(USB DP/DM)
- **V205 / X305 / X315 は common PWM pad がゼロ**。3つとも `[compile only]`
- `analogWrite()`は非PWM padでdigitalWriteにフォールバックする
  (`cores/arduino/wiring_pwm.c:139`)
