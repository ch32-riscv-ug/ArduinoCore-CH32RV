# 手動テストと実機tool

自動化できないもの、および実機を扱うための便利toolを置くディレクトリです。
手動である理由は「自動化が面倒だから」ではなく、**配線・計測器・人の判断が必要だから**です。

全体の方針・boardの階層・ペリフェラル別の検証方法は
[../TEST_PLAN.ja.md](../TEST_PLAN.ja.md)を参照してください。

## 中身

**1 case = 1 ディレクトリ**です。`<case>/<case>.py`が本体で、必要なら
`<case>.ino`と`sketch.yaml`を同居させます。

| ディレクトリ | 内容 |
|---|---|
| [`chip_info/`](chip_info/) | **いま何が繋がっているか**。probe / chip / serial port / FQBN / Serialのpin |
| [`uart_scan/`](uart_scan/) | boardがどのUSART routeを実際に配線しているか特定 |
| [`smoke/`](smoke/) | 出荷経路でcompile → upload → UART読み出し。全sketchを一巡できる |
| [`gpio_loopback/`](gpio_loopback/) | ジャンパ1本でGPIOを検証。レベル / pull-up / pull-down / 別ポートへのEXTI / PWM duty |
| [`conftest.py`](conftest.py) | 共有fixture(`attached` / `bench` / `uart_routes`)とsketchのparametrize |
| [`env_config.py`](env_config.py) | `.env`のpad名(`PA0`)をpin番号へ変換し、sketch用のheaderを書き出す |
| [`bench.json`](bench.json) | この作業台の配線記録(既定と違うboardだけ) |

**入口は`pytest`です。**

```sh
cd tests
uv run --env-file .env pytest manual/<case>/<case>.py -v -s
```

`-s`は必須です。オペレータへの指示と実機の出力が端末に出ます。

**`test_`プレフィックスは付けません。** `pytest`を引数なしで回したときに実機が要る
ものが混ざらないようにするためで、`manual`は`norecursedirs`にも入っています
(二重の防護)。手動testは常にファイルを名指しして実行します。

`chip_info` / `smoke` / `uart_scan`は**CLIとしても**動きます。
「いま何が繋がっているか」を対話的に見るような、pytestを挟む意味がない場面のためです。
どちらの経路も同じ関数(`inventory()` / `resolve_bench()` + `run()` / `scan()`)を
呼ぶので、結果が食い違うことはありません。

**設定は環境変数**です。pytestのoptionは増やしていません
(`--port`と`--target`はpytest-embeddedが既に持っていて、別の意味を重ねる方が害が大きい)。
[`../.env.example`](../.env.example)を参照してください。

**作業台固有の値は`.env`で上書きします。** どのpadが空いているかはboardごとに違うので、
sketchへ焼き込むと書いた本人以外には合いません。[`../.env.example`](../.env.example)を
`tests/.env`へコピーして編集してください。既定値は作者の作業台の値なので、
未設定でもエラーにはなりません。

pinはArduinoでは**コンパイル時**の値なので、各testの`conftest.py`がbuild前に
`env_config.h`を書き出します([`env_config.py`](env_config.py))。
`PA0`のようなpad名を`(port << 5) | bit`へ変換するので、`.env`には人が読める名前を書けます。

## boardの指定は要りません

`smoke.py`と`uart_scan.py`の`--board`は**省略できます**。probe-rsが型番を読み、
`boards.txt`の`build.probe_rs_chip`から逆引きするので、
**焼く対象と焼く相手が食い違うことが原理的に起きません**。

```sh
cd tests
uv run pytest manual/smoke/smoke.py -v -s                  # 受け入れsketch
CH32_SKETCH=all uv run pytest manual/smoke/smoke.py -v -s  # 全sketch(1件1 case)
```

```bash
uv run tests/manual/smoke/smoke.py               # 繋がっているboardで受け入れsketch
uv run tests/manual/smoke/smoke.py --sketch all  # 全sketch
```

明示したいときだけ`--board`を渡します。検出結果と食い違えば止まります
(意図的に上書きするなら`--force`)。CIのように「X035を意図してテストした」と
**主張**したい場合と、probe-rsにtargetが無い`[compile only]`のseriesでは必要です。

`--pnum`の既定は`ANY`のままです。利用者が実際にcompileするのも、profileが使うのも
`ANY`なので、勝手に実SKUへ変えるとテストしている構成が変わってしまいます。
実SKUで焼きたいときは`--pnum detect`。

## まず現状を確認する

```sh
cd tests && uv run pytest manual/chip_info/chip_info.py -v -s
```

これは他の実機testの**前提条件**そのものです: probeが応答し、chipを名乗り、
probe-rsがそのchipを知っていて、`boards.txt`が引き当て、生成されたvariantに
Serialがある——の5点を1 assertionずつ確認します。

コマンド付きのレポートが欲しいときはCLIで:

```bash
uv run tests/manual/chip_info/chip_info.py
```

```text
probe FC928F068181
  UART bridge     /dev/ttyACM4
  chip            CH32X035C8T6
  board           CH32X035
  Serial          USART1  TX=PB10  RX=PB11   (TX -> probe RX, RX -> probe TX, common ground)
  FQBN            ch32-riscv-ug:ch32v:CH32X035:pnum=CH32X035C8T6
  next            uv run tests/manual/smoke/smoke.py --board CH32X035
```

読み出しだけで、書き込みもresetもしません。
このベンチはboardを差し替えるため`/dev/ttyACM*`は固定できず、
**安定した手掛かりはprobeのUSB serial numberだけ**です。全scriptがそこから解決します。

## smoke.py

compileも書き込みも**利用者と同じ経路**(`arduino-cli upload --programmer wch-link` → probe-rs)を
通るので、passは「コードが動く」ではなく「出荷経路が動く」ことを意味します。

```bash
uv run tests/manual/smoke/smoke.py --board CH32X035
```

環境変数は要りません。toolchainとprobe-rsは`<repo>/.tools`から探します
(`uv run tools/index/fetch_tools.py`で入ります)。無ければBoard Manager導入分、
`CH32_GCC_BIN` / `CH32_PROBE_RS`があればそちらが優先されます。

既定は[Milestone 1受け入れsketch](../sketches/basic/serial_println/serial_println.ino)で、
`hello from ch32` / `int=42` / `hex=BEEF`の3行が出れば pass です。

**boardを差し替えたときは`--sketch all`**。`tests/sketches/basic/`の全sketchを
順に焼いて最後に表を出します。

```bash
uv run tests/manual/smoke/smoke.py --board CH32X035 --sketch all
```

```text
===== summary: CH32X035 (CH32X035C8T6)
  PASS  core_api
  PASS  heap_string
  SKIP  serial_echo
  PASS  serial_println
  PASS  stdio_printf
```

合否の基準は各sketchの`test_<name>.py`が`dut.expect_exact()`へ渡している
**文字列リテラル**から取ります。source of truthを1つに保つためで、
sketchを増やしてもこのtoolを触る必要はありません。加えて2つの一般規則:

- 出力に`FAIL`が含まれてはいけない
- `failures=`があれば`failures=0`でなければならない

これがあるので、`core_api`のようにparametrizeされたf-stringしか持たないtestでも
実質的に検証できます。`dut.write()`でtargetを叩くtest(`serial_echo`)はSKIPします
——このtoolは受信しかせず、刺激を書き写すとpytest側と二重管理になるからです。

書き込み前に`probe-rs info`でチップを読み、`--board`と食い違ったら止まります
(ベンチはboardを差し替えるため)。意図的に上書きするなら`--force`。

## uart_scan.py

実boardが既定と違うUSARTを配線していることはよくあります。

```sh
cd tests && uv run pytest manual/uart_scan/uart_scan.py -v -s
```

```bash
uv run tests/manual/uart_scan/uart_scan.py --board CH32X035   # -> WIRED: U1-PB10
```

testは2つです。**どれか1つのrouteが届くこと**(届かなければ配線の問題)と、
**variantが選んだrouteが届くこと**(届かなければ生成器の選択がこのboardに合っていない)。
後者が落ちたときの直し方はコード変更ではなく[`bench.json`](bench.json)への追記ですが、
気付けないと`smoke`が黙って無音になるので、testにしてあります。

結果は[`bench.json`](bench.json)へ記録すれば、以降`--serial`は要りません。
その場限りで上書きしたいときだけ`--serial <n>`。

## gpio_loopback

`core_api`のGPIO checkは**出力pinを自分で読み戻している**だけです。CH32では出力pinも
入力経路に入るので配線なしで通りますが、それが証明するのは「レジスタが往復した」ことで、
**padが実際に何かを駆動している**ことではありません。pull抵抗も、別ポートへのEXTIも、
PWMのdutyも、この方法では見えません。

ジャンパ1本(別ポートの2 pad間)で、その4つをまとめて見ます。

```sh
# 1. 空いているpadを2つ選び、ジャンパで繋ぐ
# 2. tests/.env に書く
#      CH32_LOOPBACK_OUT=PA0
#      CH32_LOOPBACK_IN=PB0
cd tests && uv run --env-file .env pytest manual/gpio_loopback/gpio_loopback.py -v -s
```

SWDのpad(PA13/PA14、X033/X035ならPC18/PC19)は**絶対に使わないでください**。
駆動した瞬間にdebug接続が切れます。他のものが繋がっているpadも同様に不可です。

ジャンパを忘れた場合は`level_through_wire`が落ちます(HIGHとLOWの両方を見ているため。
片方だけならfloating inputがHIGHを読んで誤ってpassします)。

**まだ実機で回していません**(compileのみ確認)。

## 結線

`smoke.py`は実行前に対象boardのTX/RXを表示します。**TX → probeのRX、RX → probeのTX**、GND共通。
WCH-LinkEは`ff`+CDC×2構成なので、**書き込みとSerial受信が1本のケーブルで済みます**。

`Serial`がどのUSARTになるかは**series単位**で決まります。boardの既定メニューは`ANY`
([ADR-0005](../../docs/adr/0005-board-structure-and-fqbn.ja.md))なので、
そのseriesの全型番で同じpadに出るrouteを生成器が選びます
(`tools/generate/generate.py`の`choose_uarts`)。
**routeはreset既定を最優先**します。手元のboardも、WCH公式コアも旧コミュニティコアも、
既定pinへ配線しているためです。

| board | Serial | TX | RX | route | 対応型番 |
|---|---|---|---|---|---|
| `CH32L103` | USART1 | `PA9` | `PA10` | default | 5/6 |
| `CH32M007` | USART1 | `PD5` | `PD6` | default | all |
| `CH32M030` | USART1 | `PC1` | `PC0` | default | 3/5 |
| `CH32M103` | USART2 | `PA2` | `PA3` | default | all |
| `CH32V002` | USART1 | `PD5` | `PD6` | default | 4/5 |
| `CH32V003` | USART1 | `PD5` | `PD6` | default | all |
| `CH32V004` | USART1 | `PD5` | `PD6` | default | all |
| `CH32V005` | USART1 | `PD5` | `PD6` | default | 3/4 |
| `CH32V006` | USART1 | `PD5` | `PD6` | default | 6/7 |
| `CH32V007` | USART1 | `PD5` | `PD6` | default | all |
| `CH32V103` | USART1 | `PA9` | `PA10` | default | all |
| `CH32V203` | USART1 | `PA9` | `PA10` | default | 11/12 |
| `CH32V205` | USART1 | `PB6` | `PB7` | af-2 | all |
| `CH32V208` | USART1 | `PA9` | `PA10` | default | 3/4 |
| `CH32V303` | USART1 | `PA9` | `PA10` | default | all |
| `CH32V305` | USART1 | `PA9` | `PA10` | default | 2/4 |
| `CH32V307` | USART1 | `PA9` | `PA10` | default | all |
| `CH32V317` | USART1 | `PA9` | `PA10` | default | all |
| `CH32V407` | USART1 | `PA9` | `PA10` | default | all |
| `CH32V467` | USART1 | `PA9` | `PA10` | default | all |
| `CH32X033` | USART2 | `PA2` | `PA3` | default | all |
| `CH32X035` | USART1 | `PB10` | `PB11` | default | 4/7 |
| `CH32X305` | USART1 | `PC4` | `PA12` | af-1 | all |
| `CH32X315` | USART2 | `PD6` | `PD7` | af-1 | all |

- **route**が`default`以外のboardは、`begin()`がAFIO `PCFR1`のremapフィールドを書きます
  (値はdevice-dataの`remap_fields.csv`から生成)
- `af-N`の3 series(V205/X305/X315)は**per-pin alternate-function方式でremapとは別機構**です。
  コアはまだ設定しないので**未検証**
- 「全型番」(`all`)でないboardは、小さいpackageでそのpadが出ていません。
  `ANY`で焼いた場合そのpackageではSerialが出ませんが、
  実在しないpadへの書き込み自体は無害です([ADR-0010](../../docs/adr/0010-pin-numbering.ja.md))
- この表は`variants/<board>/pins_arduino.h`から起こしたものです。生成器を変えたら更新してください

## 実機で確認済み

| 日付 | board | probe | 結果 |
|---|---|---|---|
| 2026-08-20 | CH32V003(16K/2K) | WCH-LinkE v2.12 | **pass** `serial_println` |
| 2026-08-20 | CH32V203C8T6 | WCH-LinkE v2.12 | **pass** `serial_println` + `core_api` 13 check |
| 2026-08-20 | CH32L103 | WCH-LinkE v2.12 | **pass** `serial_println`(※route変更前) |
| 2026-08-20 | CH32X035C8T6 | WCH-LinkE v2.12 | **pass** `serial_println` + `core_api` 13 check + `heap_string` + `stdio_printf` |

※ V003 / V203 / L103は**routeをreset既定優先へ変えるより前**に確認したものです。
上の表のpinは変更後の値なので、次に繋いだときに`smoke.py`を回し直してください
(X035は変更後の`PB10`/`PB11`で確認済み)。

X035は最初「UART未接続」と判定していましたが、**ケーブルが抜けていただけ**でした。
配線し直した後は`uart_scan.py`が`U1-PB10`を一発で当てています。
