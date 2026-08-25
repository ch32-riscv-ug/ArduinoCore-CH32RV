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
| [`probe_switch/`](probe_switch/) | **どのboardを繋ぐか**。USB/IPで1台をWSLへ渡し、他を外す(WSLのbench専用) |
| [`chip_info/`](chip_info/) | **いま何が繋がっているか**。probe / chip / serial port / FQBN / Serialのpin |
| [`uart_scan/`](uart_scan/) | boardがどのUSART routeを実際に配線しているか特定 |
| [`smoke/`](smoke/) | 出荷経路でcompile → upload → UART読み出し。全sketchを一巡できる |
| [`gpio_loopback/`](gpio_loopback/) | ジャンパ1本でGPIOを検証。レベル / pull-up / pull-down / 別ポートへのEXTI / PWM duty |
| [`i2c_loopback/`](i2c_loopback/) | ジャンパ2本+pull-upでWireのslaveを検証。I2C1(master)↔I2C2(slave)、データ双方向 / callback / 0xFF filler |
| [`crt0_probe/`](crt0_probe/) | 自作crt0が`setup()`へ正しいRAMを渡しているか。`.data` copy / `.bss` zero fill / `.init_array` |
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

## probe_switch.py — boardの切り替え(WSLのbenchのみ)

probeがUSB/IP(usbipd-win)で来る環境では、**同時に繋げるのは8台まで**です。
WSLの`vhci_hcd`はhigh-speed portを8本しか持たず(super-speedも8本ありますが、
USB 2.0のadapterは使えません)、埋まっていると`attach`が

```text
WSL usbip: error: no free port
```

で落ちます。**probeの故障に見えますが違います。** この作業台では8本のうち7本を
別プロジェクトのESP32が使っていたため、WCH-Linkは常に1台だけでした。
WCH-Link固有の制限ではなく、1本空ければ2台挿さります。moduleにparameterは
無い(`modinfo vhci_hcd`)ので、8本は増やせません。

そもそも**どのtoolもどのprobeを使うかを言われる必要がある**(`CH32_PROBE`)ので、
同時接続が必須のtestは今のところありません。1台だけ繋いでおけば選択が要らず、
切り替えは5秒ほどです。

```bash
uv run --env-file tests/.env tests/manual/probe_switch/probe_switch.py         # 一覧
uv run --env-file tests/.env tests/manual/probe_switch/probe_switch.py V103    # 切り替え
uv run --env-file tests/.env tests/manual/probe_switch/probe_switch.py 434A    # serialでも可
uv run --env-file tests/.env tests/manual/probe_switch/probe_switch.py --detach
```

```text
name   serial         COM    busid   state
V103   434A124C5596   COM19  16-1    attached, /dev/ttyACM0
V203   FBC18F0680B0   COM21  16-3    plugged into Windows, not attached
X035   FC928F068181   COM22  16-2    plugged into Windows, not attached
L103   0E028F0692F1   COM23  16-4    plugged into Windows, not attached
```

切り替えた後は`chip_info`と同じレポートを出すので、**何に繋がったかがその場で分かります**。

### 名前はserialで持つ

同じprobeに3つの呼び名がありますが、**安全なのは1つだけ**です。

| | |
|---|---|
| bus id `16-2` | 何かを挿し直すたびに振り直される |
| `COM19` | Windowsがdeviceごとに覚えるが、**物理portに付く**ので挿し替えると変わる |
| serial `434A124C5596` | probeに焼かれている。**唯一持ち運べる名前** |

なのでこのscriptは全部serialで解決し、bus idは毎回`usbipd state`から引き直します。
COM番号は人がDevice Managerで見るものなので表示だけします。

serialは読めないので、`tests/.env`で名前を付けます。

```sh
CH32_PROBE_V103=434A124C5596
CH32_PROBE_X035=FC928F068181
```

接頭辞`CH32_PROBE_`だけが決まりで、名前は自由です。`.env`はコミットされないので、
**この作業台のserialがrepositoryに入りません**。

### 触らないもの

外すのは**WCH-Linkだけ**です(`1a86:8010` RISC-Vモード / `1a86:8012` ARMモード)。
portを埋めている他のdeviceは挿した人のものなので、空きが要るときは
**どれが埋めているかを表示して止まります**。

繋がっていないprobeを指定したときも、**何も外さずに**止まります
(最初の版はここで動いているprobeを外してから失敗し、benchが空になりました)。

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

board定義に無い設定を試すときは`--build-property`(pytestなら`CH32_BUILD_PROPERTY`)。
分周クロックの確認はこれで再現できます。

```sh
cd tests
CH32_BUILD_PROPERTY=build.f_cpu=24000000L uv run pytest manual/smoke/smoke.py -q -s
```

**boardを差し替えたときは`--sketch all`**。`tests/sketches/basic/`の全sketchを
順に焼いて最後に表を出します。

```bash
uv run tests/manual/smoke/smoke.py --board CH32X035 --sketch all
```

```text
===== summary: CH32X035 (CH32X035C8T6)
  PASS  core_api
  PASS  heap_string
  PASS  serial_echo
  PASS  serial_println
  PASS  stdio_printf
```

`smoke.py`は[コマンド規約](../TEST_PLAN.ja.md)を喋ります。バナーは0.5秒ごとに
繰り返されるので、書き込みに何秒かかっても待てば捕まります。
**ポートはuploadの前に開いて跨いだまま**にします——バナーを捕まえるためではなく、
読み続けるためです。前のsketchはbuildとflashの20秒ほどの間も喋り続けるので、
誰も読まないとWCH-Linkのブリッジが溢れ、あとから出てくるものが混線します。そのあと
`PING <token>` → `PONG <token>`を取ってから先へ進みます——**このtoolだけは
sketchを連続で焼く**ので、前のsketchが残した`PONG`が今の`PING`の答に
見えないよう、tokenで区別する必要があります(pytest側は1ファイル1 sketchなので
バナー名で足ります)。

合否の基準は各sketchの`test_<name>.py`から読み取ります。source of truthを
1つに保つためで、sketchを増やしてもこのtoolを触る必要はありません。
各テストは1関数で、`dut.write`と`dut.expect_exact` / `dut.expect`が
**順番に並んだscript**なので、それをそのまま再生します。

| 記法 | 再生 |
|---|---|
| `dut.write("RUN\n")` | 送る |
| `dut.expect_exact("...")` | その文字列が来るまで読む |
| `dut.expect(r"...")` | 正規表現で待つ(`PASS\|SKIP`のような選択) |
| f-string | **飛ばす**。値をtestが組み立てているので再現できない |

`dut.write`が1つも無いsketchは標準の`RUN`で駆動します。加えて2つの一般規則:

- 出力に`FAIL`が含まれてはいけない
- `failures=`があれば`failures=0`でなければならない

これがあるので、`core_api`のようにparametrizeされたf-stringしか持たないtestでも
実質的に検証できます。**`dut.write()`でtargetを叩くtest(`serial_echo` /
`hooks_selftest`)もSKIPしなくなりました**——それぞれのtestファイルから
並びを読み取って再生するので、二重管理にはなりません。

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

## crt0_probe

[ADR-0003](../../docs/adr/0003-owned-startup-vector-linker.ja.md)で置き換えた自作crt0が、
`setup()`に**正しく初期化されたRAM**を渡しているかを見ます。
静的なELF等価性([`../startup/`](../startup/))では分からない部分です。

```sh
cd tests && uv run pytest manual/crt0_probe/crt0_probe.py -v -s
```

**書き込みの後・resetの前**にRAMを`0xDEADBEEF`で埋め、
C++大域constructor(crt0が動かす最初のuserコード)が見た値をSerialで報告させます。
埋めるのが後なのは、**書き込みalgorithm自体がRAMを使う**からです。

埋めることが結果に意味を与えます。「`.bss`が0だった」はRAMが元から0の石では何も
証明しないので、同じ実行で**`_ebss`の先にパターンが残っていること**も確認します
(何も初期化しない領域なので)。これが落ちたら他の3つは無効です。

```text
data_at_ctor=A5A5A5A5   .data がflashからcopyされた
bss_at_ctor=00000000    .bss がzero fillされた
ctor=C0DEC0DE           .init_array が実行された
past_ebss=DEADBEEF      埋めパターンが実際に届いていた(対照)
```

boardを載せ替えれば同じコマンドで回ります。**variantごとのlinker scriptとvector table**の
検証になるので、新しいseriesを触るときは最初にこれです。

sketchが`sketch/`にあるのは、testディレクトリ直下の`*.ino`をpytest-embeddedが
自分のsketch testと解釈して`sketch.yaml`を要求するためです。これはdriverがcopyして
自分でbuildする素材で、uploadとresetの間にRAMを埋める必要があります。

**2026-08-25にコマンド規約へ載せました。** このtestはdriverが自分でresetするので
「最初の出力を取り逃す」問題は元から無いのですが、載せた理由は別で、
**言うだけ言って黙るsketchは最後の1行をWCH-Linkのブリッジに置き去りにする**ためです
(繰り返すバナーがパイプを動かし続けます)。固定時間の読み取りも要らなくなり、
バナーを待って`RUN`を送る形になりました。

`bss_zeroed` / `data_copied_from_flash` / `init_array_ran`はboard側の
`tc_check`になりましたが、**対照の`past_ebss`だけはhost側に残してあります**。
パターンを決めているのがhostなので、boardにも同じ定数を置くと二重管理になって
ずれます。

**`RUN`の前に`PING`を通します。** 最初の版はbannerを見たらすぐ`RUN`を撃っていて、
V103だけ応答が来ませんでした。transcriptを見ると**PONGが返るまでにbannerが7回**
出ていて、WCH-Linkのブリッジが最初の1往復に数秒かかっているだけでした。
`smoke.py`が既に同じ理由でhandshakeを入れています。

4 board(V103 / V203 / X035 / L103)で5 passを確認済みです。

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
| 2026-08-25 | CH32V103R8T6 | `434A124C5596` | **12/12 pass** (`--sketch all`) |
| 2026-08-25 | CH32V203C8T6 | `FBC18F0680B0` | **12/12 pass** |
| 2026-08-25 | CH32X035C8T6 | `FC928F068181` | **12/12 pass** |
| 2026-08-25 | CH32L103C8T6 | `0E028F0692F1` | **11/12** — `tone_selftest` FAIL ([原因](#ch32l103のtoneが鳴らなかった-2026-08-25-解決)) |
| 2026-08-25 | 上記4台 | 同上 | 32 bitタイマの修正後に**再走。全4台 12/12 pass** |

`probe_switch`でprobeを切り替えながら`smoke.py --sketch all`を回したものです。
**実機検証がV103の1 familyだけだった状態は解消**しました。

修正後の再走には、確認したかったことがもう1つあります。
**CH32V203のboard定義は`CH32_TONE_TIMER_BITS 32`を出しますが、ベンチのC8T6の
TIM4は16 bit**です。そこで`tone_selftest`が通ったので、
**16 bitレジスタへの32 bitストアは実機で無害**——1つのboard定義が32 bit部品と
16 bit部品を混在させていても安全、と確認できました。

### CH32L103の`tone()`が鳴らなかった (2026-08-25、解決)

**CH32L103のTIM4は32 bitタイマ**でした。coreは`ATRLR`を16 bitストアで書いており、
**32 bitレジスタへの16 bitストアは上下両方のhalfwordへ複製されます**。

```text
CH32_TIM_ATRLR(TIM4) = 7999      ->  32 bitで読むと 0x1F3F1F3F
```

7999(0x1F3F)のつもりが524,550,463になるので、1 ms周期のはずが**65秒周期**になり、
update eventが事実上来ません。TIM2/TIM3は16 bitなので同じストアで正しく、
だから`analogWrite()`(TIM1/2/3)もServo(TIM3)も無事でした。

切り分けは実機のレジスタダンプで、次の順に潰しました。

| 見たもの | 結果 |
|---|---|
| RCC APB1のTIM4EN | 立っている |
| PFIC(IENRは**write only**なので`ISR[]`を読む) | IRQ 46は有効 |
| TIM2 / TIM3を同じ手順で | `cnt_high_5ms≈7999`、5 msで5回割り込み——**正常** |
| TIM4 | `cnt_high_5ms=65517`、`INTFR=0`——**ATRLRを無視して0xFFFFまで走る** |
| TIM4に`SWEVGR=UG`を書く | `INTFR=0x1`——**flagそのものは生きている** |
| TIM4のATRLRを32 bitで書く | `cnt_high_5ms=7994`、5 msで5回割り込み——**正常** |

裏付けはEVTの`ch32l103.h`にもあります。`TIM_TypeDef`が
`ATRLR_TIM4` / `CNT_TIM4` / `CHnCVR_TIM4`を**32 bitのunion**で持っていて、
16 bit版と並んでいます。データシートの機能表も
「General-purpose TIM4 (32-bit)」と書いています。

対処は、変異体が`CH32_TONE_TIMER_BITS`(と`CH32_SERVO_TIMER_BITS`)を出し、
`ch32_registers.h`の`CH32_TIM_ATRLR32()`を使い分けるようにしたことです。
修正後、L103は**12/12 pass**。

**同じ形の32 bit TIM4を持つfamilyは他にもあります**——EVTヘッダで
CNTとATRLRがunionになっているのは`ch32l103.h` / `ch32v205.h` / `ch32v20x.h` /
`ch32x3x5.h`の4つで、つまり
**CH32L103 / CH32M103 / CH32V203 / CH32V205 / CH32X305 / CH32X315**。
いずれも`CH32_TONE_TIMER`が4です。実機で確認できたのはL103だけで、
残りは手元にありません(V203C8T6はその型番のTIM4が16 bitなので元から通っていました)。

X035は最初「UART未接続」と判定していましたが、**ケーブルが抜けていただけ**でした。
配線し直した後は`uart_scan.py`が`U1-PB10`を一発で当てています。
