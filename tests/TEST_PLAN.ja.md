# テスト計画（入口の一覧と実行手順）

> English: [TEST_PLAN.md](TEST_PLAN.md)

方針・層の切り方・ベンチの定義・プローブの準備・自動と手動の境界・移行の順序は
[docs/development-workflow.ja.md](../docs/development-workflow.ja.md)（決定、2026-09-30）にあります。ここはその写しではなく、
**何がどこにあり、どう回すか**だけを書きます。

## 層とディレクトリ

ディレクトリは「何を確かめるか」ではなく「**何を要するか**」で切ります（pytest-embedded-arduino-cli のガイドの規則）。
各ディレクトリが 1 つの回し方に対応します。

| ディレクトリ | 要るもの | 回し方 | CI |
|---|---|---|---|
| `unit/` | 何も要らない | `uv run pytest unit`（数秒） | 全 PR |
| `build/` | toolchain、arduino-cli、device-data（`uv run tools/index/fetch_tools.py`） | `uv run pytest build`（`-m "not slow"` で数秒、全部で数分） | 全 PR（sweep は専用 job） |
| `bench/` | 常設ベンチ 1 台（probe + DUT、`--profile` の先） | `uv run --env-file .env pytest bench --profile <board>` | 正式ベンチの self-hosted runner（未） |
| `manual/` | 人か臨時の機材 | ファイルを名指し: `uv run --env-file .env pytest manual/<case>/<case>.py -s` | 無し |
| `benches/` | （データ）治具の定義 `<name>.toml` | `TEST_BENCH_<PROFILE>` が指す | — |

`bench/` と `manual/` は `norecursedirs` に入っていて、引数なしの `pytest` が実機を焼くことはありません。`manual/` の入口は
`test_` を付けず、ファイルを名指ししたときだけ走ります（二重の防護）。この規約自体は `unit/test_tests_layout.py` が検査します。

## 入口の一覧

### unit/（何も要らない）

| 入口 | 何を見るか |
|---|---|
| `test_tests_layout.py` | この計画の規約そのもの（カテゴリ、接頭辞、conftest の唯一性、入口の一覧との一致） |
| `test_board_layer.py` | board レイヤの定義権限（variant が `LED_BUILTIN` を定義しない、`requires:` が実在 capability を指す） |
| `test_uiapduino_board.py` | UIAPduino V1.4 専用 board、HID upload recipe、pin 番号の互換 |
| `test_peripheral_table.py` | `docs/peripheral-support.ja.md` の○が device-data の clock enable と矛盾しない |
| `test_startup_parameters.py` | startup harness の march / mabi / startup 定義が boards.txt の生成値と一致 |
| `test_adc_instances.py` | ADC instance を持つ variant で pad と channel が揃う |
| `test_clock_prescaler.py` | AHB 分周器の符号化表（compile 時 assert、riscv gcc だけ要る） |
| `test_pd_frames.py` | USB PD のフレームロジック（host の cc で共有ライブラリにして ctypes） |

### build/（toolchain が要る）

| 入口 | 何を見るか |
|---|---|
| `generated/test_generated.py` | boards.txt / variant / vector include が device-data の locked commit からの再生成と一致。`sync_profiles.py --check`、`sync_testcmd.py --check` |
| `vendor/test_vendored_api.py` | ArduinoCore-API snapshot が lock の commit と同一 |
| `vendor/test_vendored_tinyusb.py` | TinyUSB snapshot が lock の SHA-256 と一致 |
| `vendor/test_udev_rules.py` | platform ルートの `60-ch32rv.rules` が同梱 ch32rv の rule（archive、`doctor --emit-udev`）と byte 一致、`post_install.sh` がある（B-6） |
| `startup/test_startup_equivalence.py` | 自作 crt0 と EVT startup の ELF 等価性（`slow`、EVT mirror 要） |
| `startup/test_interrupt_tables.py` | interrupts.csv が EVT startup assembly と一致（EVT mirror 要） |
| `compile/test_compile_matrix.py` | 全 part number の compile と size baseline（`slow`） |
| `compile/test_uiapduino_compile.py` | UIAPduino の `pnum=ANY` で FQBN 解決から link まで |
| `compile/test_examples.py` | 同梱 examples が代表 2 枚（X035 / V003）で compile（`slow`） |
| `compile/test_examples_sweep.py` | 同梱 examples が全 24 series で compile（`--sweep`、約 20 分、Actions 専用） |
| `sizebench/test_sizebench.py` | newlib のサイズ計測 harness（`slow`） |
| `package/test_package_install.py` | 生成した index から clean install → 上書きなし compile → upgrade / rollback（`slow`） |
| `sketches/test_sketch_profiles.py` | bench の全 sketch × sketch.yaml の全 board を compile（`slow`） |
| `sketches/test_sketch_profile_build.py` | 同梱 examples の `arduino-cli compile --profile`（版 pin、loopback の index 経由）で全 example × 全 profile（`slow`）。bench の profile は版無し（作業ツリー）なのでここでは見ない |

`build/sketches/` には bench の sketch の道具も置きます: `testcmd.h`（コマンド規約の原本。各 case へ `sync_testcmd.py` が配る）、
`sync_profiles.py`（`sketch.yaml` の `profiles:` を生成）、`stage.py`、`compile_all.py`、`profile_build.py`。

### bench/（常設ベンチ）

**1 case = 1 ディレクトリ、1 module 1 test が原則**（section の独立した trace は 1 module に複数 test）。中身は
`<case>.ino`、`sketch.yaml`（生成物）、`testcmd.h`（生成物）、`test_<case>.py`。fixture は
pytest-embedded-arduino-cli（`dut`）、pytest-embedded-arduino-cli-ch32rv（`ch32rv` / `oep_host` / `ch32_uart`）、
pytest-embedded-wireskein（`ws_run`）、そして `bench/conftest.py` の `bench`（治具の定義）です。共有コードは
`bench_kit.py`（READY / PING / UART の指名）と `tracekit.py`（OEP の plan / capture / gpio / uart、decoder）。

| 場所 | case | 何を見るか | 要る治具 |
|---|---|---|---|
| `basic/` | `serial_println` `serial_echo` `core_api` `heap_string` `print_format` `stdio_printf` `route_selftest` `spi_selftest` `wire_selftest` `tone_selftest` `servo_selftest` `pd_selftest` `hooks_selftest` `system_selftest` | 自己検査（sketch が PASS / FAIL を出し、host は順に読む）。UART の 3 本は probe の UART を相手にする | どのベンチでも（UART は `[uart]`） |
| `trace/` | `periph_probe`（PWM / tone / timing / SPI / SPI peer） | 線上の実測。capture で記録し、WireSkein が照合 | OEP + capture、`facts.pwm` / `facts.spi` |
| | `gpio_probe` | 配線された全 pad を両側から駆動・観測、EXTI | OEP + fixture.gpio、`[wiring]` |
| | `adc_probe` | analogRead を probe の rail で、X035 の無い channel の判定 | OEP、`facts.adc` |
| | `reset_probe` | software / debug reset → setup() の時間、resetReason | OEP + capture、`facts.pwm` |
| | `i2c_probe` | Wire master を線上で decode（write / read / repeated START / 400 kHz / setClock / stretch / stuck bus） | OEP + I2C target + capture、`facts.i2c` |
| | `uart_probe` | UART を probe の UART と両方向、overflow、64 KiB、reset 後; sweep（`slow`）は F_CPU × baud × format と線上の baud | OEP + fixture.uart、`[uart]` |
| `startup/` | `crt0_probe` | crt0 が渡す RAM（probe が RAM を埋めて reset） | OEP |
| `regs/` | `reg_probe` | デバッガでレジスタを読み device-data と照合（方法 3）。console は bench の UART（halt / resume を挟むと dmseq は数秒黙る） | OEP（oep-probe-arduino 0.0.7 以降）、device-data |

治具に無いもの（pad、capture、I2C target）を要する test は理由付きで **skip** します。skip の一覧が「このベンチが確かめていないこと」です。

### manual/（人か臨時の機材）

| 入口 | 何をするか |
|---|---|
| `gpio_loopback/gpio_loopback.py` | ジャンパ 1 本で GPIO（レベル / pull / 別ポートの EXTI / PWM duty）。LinkE 経路 |
| `i2c_loopback/i2c_loopback.py` | ジャンパ 2 本 + pull-up で Wire の slave。LinkE 経路 |
| `uiapduino_fixture/` `uiapduino_pin_map/` `uiapduino_timer_fixture/` | UIAPduino の治具と pin 表 |
| `chip_info/chip_info.py` `uart_scan/uart_scan.py` `probe_switch/probe_switch.py` | ベンチの道具（何が繋がっているか / どの USART route か / USB/IP の切り替え）。LinkE 経路 |
| `smoke/smoke.py` | 上の道具が共有する LinkE 経路のライブラリ（sketch の再生は bench/ に移り、無くなった） |

LinkE 経路のものは probe-rs / ch32rv を直接呼ぶ古い形で、正式ベンチ（家系ごとの P4）が揃ったら bench/ に移すか消します。

## 実行手順

```sh
uv run tools/index/fetch_tools.py         # toolchain / device-data を <repo>/.tools へ（版は tools/index/tools_*.json）
cd tests && uv sync
cp .env.example .env                      # このベンチの port と bench file（TEST_SERIAL_PORT_<P>、TEST_BENCH_<P>）
```

| 場面 | コマンド |
|---|---|
| 保存のたび | `uv run pytest unit` |
| コミット前 | `uv run pytest`（unit + build、約 7 分。`-m "not slow"` で数秒） |
| 触った機能 | `uv run --env-file .env pytest bench/<dir>/<case> --profile <board> -s` |
| ベンチ 1 台の全部 | `uv run --env-file .env pytest bench --profile <board>` |
| マージ前 / リリース前 | `uv run pytest --clean` と、各ベンチの `pytest --clean bench --profile <board>` |
| プローブを焼き直す / 設定を戻す | `uv run --env-file .env bench/prepare.py --profile <board>`（`--check` は照合だけ） |
| ch32rv の同梱版を上げる | `uv run tools/index/bump_tool.py ch32rv <version>` |
| リリース | GitHub Actions の Release（`bump_version.py` が platform.txt と sketch.yaml を同時に動かす） |

`pytest bench` は収集の直後にプローブを bench file と照合し、合わなければ usage error で止まります（`prepare.py` を案内）。
ベンチ上の作業は同時に 1 つです。

## 関連

- [docs/development-workflow.ja.md](../docs/development-workflow.ja.md) — 方針と決定
- [docs/oep-workflow.ja.md](../docs/oep-workflow.ja.md) — OEP を含む最終の形
- [tests/README.ja.md](README.ja.md) — セットアップと道具の在処
- [tests/manual/README.ja.md](manual/README.ja.md) — 残っている手動試験と道具
