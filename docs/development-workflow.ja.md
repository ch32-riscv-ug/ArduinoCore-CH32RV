# 開発ワークフローとテスト計画

文書基準日: 2026-09-30。状態: **決定**（2026-09-30。§12 の判断は決まり、§11 の 1〜6 は実装済み、7 は正式ベンチ待ち）。
[oep-workflow.ja.md](oep-workflow.ja.md)（OEP を含む最終の形）の「§7 pytest の道具」「§9 β」を、
日々の作業とベンチの運用まで含めて 1 本にしたもの。個々の道具の仕様は各リポジトリが正で、ここは分担と手順を置く。

前提（2026-09-30 の指示）:

- 常設のベンチは **1 家系につき 1 台の P4 プローブ + 開発基板**。配線はできる限り多く付ける。いまつながっているベンチは
  正式なものに入れ替える予定で、そのときに固定する。**それまでの実機の実行は全部 manual 相当**。
- ベンチの情報（port、probe、配線、プローブの版）は**ハードコードせず `.env` などで管理**する。プローブの想定版はそこに書き、
  テストの最初に照合する。合わなければ手順書とコマンドで人が直す。
- 何度も繰り返す作業（ch32rv の版上げ、プローブの焼き直し、版の同期）は**コマンド 1 つ**にする。
- 自動と手動の境界をはっきり決める。

## 1. 原則（pytest-embedded-arduino-cli のガイドから採るもの）

| 原則 | ここでの形 |
|---|---|
| ディレクトリは「何を確かめるか」ではなく「**何を要するか**」で切る | `unit/`（何も要らない）、`build/`（toolchain）、`bench/`（常設ベンチ 1 台）、`manual/`（人か臨時の機材） |
| `unit/` はボード無しで動き、CI に載る | 現状どおり。`build/` も hosted runner で動く |
| **1 module 1 テスト**。粒度は device 側（`tc_check` の PASS/FAIL 行）で稼ぐ | 現状の sketch 規約（`testcmd.h`）どおり。`RUN` 一発で全 check が返る |
| 起動時の 1 回の通知を待たない。READY を繰り返す、または問い合わせる | 現状どおり（0.5 s ごとの `<name> READY`、token 付き PING） |
| **primary の port が無いのは設定の誤り**。skip ではなく失敗 | `bench/` は `--profile` と `TEST_SERIAL_PORT_<PROFILE>` が揃っているときだけ回す |
| 既定から外すものは `norecursedirs`（ディレクトリ名指しで走る）か接頭辞なし（ファイル名指しでだけ走る） | `bench` は前者、`manual` は両方（最も厳しい形） |
| 書き込みで消えない状態に頼らない。**前回の実行に依存した失敗は再現しない** | プローブは「全消去 → 決めた版を焼く → 設定を流し込む → 照合」の手順を持つ（§5）。DUT は upload ごとに消える |
| `.env` は machine 固有、git に入れない。`.env.example` が説明 | 現状どおり。ベンチの**配線は治具の設計**なので別ファイルで commit する（§4） |
| conftest は最後の手段。fixture は plugin に | ベンチの fixture は `pytest-embedded-arduino-cli-ch32rv` と `pytest-embedded-wireskein` が持つ。`tests/bench/conftest.py` は前提の照合（§6）だけ |
| 失敗は fixture health（環境）と core の回帰で別の結果にする | 前提の照合は `error`（環境）、sketch の check の FAIL は `failed`（回帰） |

## 2. 層（安い順。下は上に答えられない問いに答える）

| # | 層 | 何が要るか | いつ | 入口 |
|---|---|---|---|---|
| 1 | **unit** | 何も要らない（Python、host の cc） | 保存のたび、全 PR | `pytest unit`（数秒） |
| 2 | **build** | toolchain、arduino-cli、device-data | 全 PR | `pytest build`（`-m "not slow"` で数秒、全部で数分）、sweep は Actions 専用 job |
| 3 | **bench** | 常設ベンチ 1 台（P4 + DUT） | 触った機能の module は作業中、全部はマージ前 / リリース前 | `pytest bench/<case> --profile <board>`、`pytest bench --profile <board>` |
| 4 | **package** | index を作って loopback で配り、まっさらな data dir に install | リリース前、CI | `pytest build/package`（現 `package/`）|
| 5 | **manual** | 人、臨時の機材、配線替え | 必要なとき | `pytest manual/<case>/<case>.py -s` |

`bench` の全部は**ベンチ 1 台につき 1 回の pytest**（`--profile` 1 つ）。家系の数だけ順に回す（ベンチ上の作業は同時に 1 つ）。
「1 回の起動で、きれいな状態から、全部」は `pytest --clean bench --profile <board>`。

## 3. ディレクトリ（作り直し後）

```text
tests/
  README.ja.md / TEST_PLAN.ja.md          この文書の実行手順の写しと、入口の一覧
  pyproject.toml / uv.lock                依存に pytest-embedded-arduino-cli(-ch32rv) と pytest-embedded-wireskein を含める
  conftest.py                              --clean / --sweep / tool の在処だけ（現状）
  loader.py / sketch_requirements.py       共有モジュール（現状）
  .env.example / .env                      ベンチ固有の値（§4）
  benches/                                 治具の定義（配線・期待するプローブの版・スロット・bind・plan）。commit する
    x035-p4.toml
    v003-p4.toml …
  unit/                                    現状のまま
  build/                                   toolchain が要る自動テスト。現 generated / vendor / startup / compile / sizebench /
                                           package と、sketches/test_sketch_profiles.py・test_sketch_profile_build.py
  bench/                                   常設ベンチで回す自動テスト。norecursedirs
    conftest.py                            前提の照合（§6）。fixture は足さない
    prepare.py                             プローブの準備（§5）。harness、test_ なし
    <case>/
      <case>.ino  sketch.yaml  testcmd.h   現 sketches/basic/<case> のまま（sketch.yaml と testcmd.h は生成物）
      test_<case>.py                       現 bench_<case>.py を改名。1 module 1 テスト
    trace/<case>/ …                         現 manual/oep_*_trace を pytest 化したもの（oep_host + ws_run）
    regs/…                                  現 manual/reg_probe を oep_host の riscv-dm 経由に
  manual/                                  人か臨時の機材が要るもの。接頭辞なし + norecursedirs（現状）
    gpio_loopback / i2c_loopback / uiapduino_* / …
  tools/                                   ベンチの道具（試験ではない）。現 chip_info / uart_scan / probe_switch
```

消えるもの: `sketches/*/expect.py`、`manual/smoke/smoke.py`、`manual/oep_smoke/{oep_smoke,trace_kit,targets}.py`、
`manual/conftest.py` の probe-rs 依存の fixture、`manual/bench.json`、`manual/env_config.py`（pin は実行時にコマンドで渡す。
既に `reg_probe` の TESTCMD がそう）、probe-rs の残り（`tools_probe_rs.json`、`probe_rs` fixture、`.tools` の取得）。

`unit/test_tests_layout.py` の `CATEGORIES` を `unit / build / bench / manual / tools / benches` に置き換える。

## 4. ベンチの定義: `.env` と `benches/<name>.toml`

**設計（配線）と個体（識別子）を分ける。** 配線は治具の設計で、同じ治具を作れば同じになるので commit する。
どの個体がつながっているか、どの port に見えるかは machine ごとなので `.env`。

`.env`（profile 名 = board 名 = ベンチ名）:

```sh
# pytest-embedded-arduino-cli の規約（既存）
TEST_SERIAL_PORT_CH32X035=oep://30eda0e31108-hs/x035     # IDE port。upload / monitor / oep_host がここから probe を引く
# このプラグイン群の規約（既存 + 追加）
TEST_UART_CH32X035=4,0                                     # DUT のどの USART / route が probe に届いているか
TEST_BENCH_CH32X035=x035-p4                                # benches/x035-p4.toml
TEST_BENCH_CH32X035_UNIT=30eda0e31108                      # 個体（describe の unit id）。TOML の unit と一致することを照合
```

`benches/x035-p4.toml`（治具の設計。commit）:

```toml
[probe]
model    = "esp32-p4-devkit"                  # describe の model
firmware = "3.2.0-v1rc"                       # describe の firmware。合わないと bench は error（§6）
profile  = "io.github.ch32-riscv-ug.p4-x035"  # describe の profile
release  = "oep-probe-arduino v0.0.4"         # prepare.py が焼く版（例: examples/Esp32P4X035Probe）
example  = "Esp32P4X035Probe"
erase    = "all"                              # 焼く前に全消去（P4/ESP32: EraseFlash=all、RP2350: flash_nuke）

[dut]
board = "CH32X035"                            # profile と一致
part  = "CH32X035F8U6"                        # 錠にはしない（§5.2 の方針）。記録
usb_pads = ["PC16", "PC17"]                   # 治具の注意（USB PHY の pad）

[slot]                                        # oep config slot
name = "x035"; wire = "rvswd"; pins = [2, 54]; attach = "at-boot"; retry_s = 1; mechanism = "dmseq"

[[bind]]                                      # oep config bind
port = 1; mode = "last-reset"; stream = "slot:x035"
[[bind]]
port = 3; mode = "last-reset"; stream = "slot:x035"

[plan."oep.fixture.uart#1"]                   # oep config plan（fixture UART）
rx = 12; tx = 6                               # DUT USART4 route 0: PB0(TX) -> 12, PB1(RX) <- 6

[wiring]                                      # DUT pad -> probe channel。ここに無い pad は「配線されていない」
PA0 = 46; PA1 = 47; PA2 = 48; PA3 = 49; PA4 = 53; PA5 = 4; PA6 = 11; PA7 = 5
PB0 = 12; PB1 = 6; PB3 = 13; PB11 = 9; PB12 = 14; PC14 = 10; PC15 = 15; PC16 = 52; PC17 = 50

[facts]                                       # 治具の測った事実。テストがしきい値に使う
adc_absent   = ["PA3", "PA7"]                 # errata x035-adc-ch-i2c-unavailable のこの個体
adc_high_min = 960
external_pullup = []
capture_max_hz = 20_000_000
```

テストは pad 名で要求する（`bench.channel("PA1")`）。配線にない pad を要求したテストは**理由付きで skip** し、その skip の一覧が
「このベンチが確かめていないこと」の報告になる（harness-testing §8 の考えを最小で）。LinkE のベンチ（正式ベンチが揃うまでの
経過措置）は `[probe] model = "WCH-LinkE"`、`firmware = "2.22"`（`ch32rv probe list --json` の `firmware.norm`）、配線は UART だけで、
capture や gpio を要するテストは skip になる。

## 5. プローブの準備 `bench/prepare.py`（繰り返す作業をコマンドに）

```sh
uv run --env-file .env tests/bench/prepare.py --profile ch32x035            # 全部やる
uv run --env-file .env tests/bench/prepare.py --profile ch32x035 --check    # 照合だけ（pytest の最初と同じ）
```

1. **全消去して焼く**。TOML の `release` の oep-probe-arduino の tag を取り（`git archive`、または Release の ZIP）、その中の
   `examples/<example>` を release の `sketch.yaml`（library の版を pin している）で `arduino-cli compile --clean` し、
   `EraseFlash=all` を付けて upload。RP2350 は flash_nuke の UF2 → firmware の UF2。ビルドの platform 版も release の
   `sketch.yaml` が固定している（arduino-esp32 3.3.12 など）。
   - **選択肢**: oep-probe-arduino の Release に example ごとの**ビルド済み binary**を付けてもらえば、prepare は焼くだけになり、
     ベンチ側に ESP32 の toolchain が要らなくなる（dev_oep への依頼。§12）。
2. **設定を流し込む**。TOML の `[slot]` / `[[bind]]` / `[plan]` を `oep config slot / bind / plan … --save` で書く
   （既存の設定は先に `erase`）。
3. **照合する**。describe の firmware / model / profile / unit id と、`oep config show --json` の slot / bind / plan が TOML と一致
   することを確かめる。`--check` はこの段だけ。
4. WSL のベンチだけ: 焼き直しで HS の口が外れるので `usbipd attach` をやり直す（`TEST_BENCH_<P>_BUSID` があれば）。正式ベンチが
   Linux 直結ならこの段は無い。

LinkE のベンチは 1〜2 が無く（firmware は WCH のツールでしか更新できない）、3 の照合だけ。

## 6. pytest の最初の照合（`bench/conftest.py`、session の autouse）

prepare の 3 と同じ関数を呼ぶ。1 秒未満、ロック無し（describe と config show は lock-free の読み）。

| 状況 | 結果 |
|---|---|
| `TEST_BENCH_<P>` が無い、TOML が無い | **error**（設定の誤り。skip にしない） |
| probe の firmware / profile / unit が TOML と違う | **error**、「`prepare.py --profile <p>` を回してください」と手順を出す |
| slot / bind / plan が違う | 同上 |
| DUT が応答しない、check が FAIL | **failed**（core の回帰） |
| テストが要求した pad が `[wiring]` に無い | **skip**（理由: `PA9 is not wired on x035-p4`） |

「照合は cheap、焼き直しは高い」ので、pytest は照合だけ。焼き直すのは人が prepare を回すとき（版を上げたとき、状態が壊れたとき）。

## 7. 自動と手動の境界

**自動（bench/）**: 常設ベンチが人なしで実行でき、判定がアサーションで書けるもの。

| 対象 | いま | 作り直し後 |
|---|---|---|
| basic 14 sketch（Serial / heap / printf / GPIO / 時刻 / route / SPI / Wire / tone / servo / PD / hooks / system） | `bench_*.py`（pytest）と runner の二重 | `bench/<case>/test_<case>.py` だけ |
| 線上の実測（PWM / tone / SPI mode / UART / GPIO 両側駆動 / ADC rail / reset / I2C decode） | `manual/oep_*_trace`（自前 runner + trace_kit） | `bench/trace/<case>/test_<case>.py`。`oep_host` + `ws_run`（pytest-embedded-wireskein）。capture の無いベンチは skip |
| レジスタ照合（GPIO / AFIO / USART / EXTI / TIM / ADC / DAC / I2C / SPI / RCC / PFIC） | `manual/reg_probe`（1 レジスタ 1 プロセス、30〜130 s） | `bench/regs/…`。`oep_host` の riscv-dm `read_block` でセッション 1 本（ms 単位） |
| crt0 が渡す RAM | `manual/crt0_probe`（driver が upload と reset の間に RAM を埋める） | `bench/crt0_probe`。upload 後に `oep_host` で halt → RAM を埋める → reset。要工夫だが自動にできる |
| Board Manager install → compile | `package/`（CI） | `build/package`（変更なし） |

**手動（manual/）**: 人の操作、配線替え、臨時の機材、外部との実 negotiation。

| 対象 | 理由 |
|---|---|
| gpio_loopback / i2c_loopback（ジャンパ） | 配線替え。正式ベンチで常設配線にすれば bench/ へ移せる |
| UIAPduino の HID upload | 手動の pin reset |
| Windows / macOS の install・upload・monitor の実機確認 | 別 OS の machine |
| USB-PD の実 negotiation、ロジアナ外部機材 | 専用治具 |
| ベンチの道具（chip_info / uart_scan / probe_switch） | 試験ではない → `tests/tools/` |

**それまでの間**: 正式ベンチが無いので、`pytest bench --profile <board>` を人が回すのが manual 相当。自動化の対象にはしない。

## 8. 日々の流れ（コマンド）

| 場面 | コマンド |
|---|---|
| core を触った | `cd tests && uv run pytest unit` → `uv run pytest -m "not slow" build` |
| ある機能を触った | `uv run --env-file .env pytest bench/<case> --profile <board>`（ベンチ 1 台） |
| コミット前 | `uv run pytest`（実機なしの全部。約 7 分） |
| マージ前 / リリース前 | `uv run pytest --clean`、各常設ベンチで `uv run --env-file .env pytest --clean bench --profile <board>`、CI の install-test |
| プローブの版を上げた / 状態が変 | `benches/<name>.toml` の `release` / `firmware` を書き換え → `prepare.py --profile <board>` |
| ch32rv の版を上げた | `uv run tools/index/bump_tool.py ch32rv 0.12.2`（§9） |
| platform の版を上げる / リリース | Actions の Release（既存。`bump_version.py` が platform.txt と sketch.yaml を同時に動かす） |
| 依存の版が上がった（oep-client / wireskein / plugin） | 下限を上げるだけ（pin しない。壊れたら直す） |

## 9. 繰り返す作業のコマンド化

| 作業 | いま | コマンド |
|---|---|---|
| ch32rv の同梱版を上げる | 手で JSON を書き換え、.sha256 を 5 つ取り、fetch_tools で確かめる | `tools/index/bump_tool.py <tool> <version>`: Release の assets から `tools_<tool>.json` を作り直し、fetch して checksum と `--version` を確かめ、CHANGELOG の Unreleased に雛形の行を足す |
| プローブの焼き直しと設定 | 手順を毎回組む（compile → upload → attach → config） | `tests/bench/prepare.py`（§5） |
| プローブの状態の確認 | `oep config show`、`oep dump` を目で見る | `prepare.py --check`（pytest の最初と同じ） |
| sketch.yaml / testcmd.h の同期 | `sync_profiles.py` / `sync_testcmd.py`（既存） | 変更なし。`generated/`（→ `build/`）が `--check` |
| 実機の全ベンチ一巡 | 家系ごとに手でコマンドを打つ | `tests/bench/run_all.py`: `.env` にある profile を順に、ベンチ 1 台ずつ `pytest bench --profile` を回し、profile ごとの結果と skip の一覧を 1 枚にまとめる |

## 10. CI

| job | どこ | 内容 |
|---|---|---|
| unit / build（現 5 job） | hosted、全 PR | 変更なし（ディレクトリ名の置き換えのみ） |
| example-sweep | hosted、専用 job | 変更なし |
| install-test | hosted、3 OS | 変更なし |
| **bench** | **self-hosted runner**（正式ベンチの host） | `run_all.py`。main への push と release 候補。fork の PR からは回さない（test-strategy の安全の規則） |

正式ベンチができるまで bench の job は作らない（手で回す）。

## 11. 移行の順序と進捗（2026-09-30）

| # | 作業 | 状態 |
|---|---|---|
| 1 | `benches/*.toml` と `.env` の規約。X035（P4）と LinkE 7 台分。`prepare.py --check` と `bench/conftest.py` の照合 | 済 |
| 2 | `sketches/basic/*` → `bench/basic/*`、`bench_*.py` → `test_*.py`。profile build の 2 テストは `build/sketches`。layout test、CI、`pyproject` の依存にプラグイン | 済（X035 P4 14/14、V203 LinkE 14/14） |
| 3 | `prepare.py` の焼き直し（Release の tag から example をビルド、全消去、設定、照合、WSL の usbipd 付け直し） | 済（X035 ジグで全工程） |
| 4 | trace 6 本を `bench/trace/` に（`trace_kit.Run` → `ws_run`、`targets.py` → bench file、pin は実行時に） | 済（X035: periph 5/5、gpio 1/1、adc 2/2、reset 1/1、i2c 5/5、uart 2/2） |
| 5 | `crt0_probe` を probe が RAM を埋める形で自動に。`reg_probe` を `oep_host` の riscv-dm 経由に | crt0 済（1/1）。reg_probe は **probe の halt/resume が core のレジスタを戻さない不具合**で xfail（dev_oep が d1fe654 で直し、0.0.6 待ち） |
| 6 | 旧 runner（`oep_smoke`、`oep_*_trace`、`reg_probe`、`crt0_probe`、`expect.py`）を消す。TEST_PLAN / README を書き直す。役目を終えた docs に注記 | 済。LinkE 経路の道具（chip_info / uart_scan / probe_switch / smoke.py）とジャンパの試験は manual に残る（正式ベンチまで）。probe-rs の残り（`tools_probe_rs.json`、fetch）は smoke.py が使うので同時に消す |
| 7 | 正式ベンチが届いたら家系ごとに TOML、`prepare.py` で立ち上げ、`run_all.py` で一巡 → self-hosted runner | 未（`run_all.py` も未） |

残っている probe 側の依頼: capture の edge trigger（backlog）、plan_roles の宣言（0.0.6）、DATA0 / GPR の復元（0.0.6）。
0.0.6 を焼いたら reg_probe の xfail を外し、console を dmseq に戻せるか（halt/resume を挟んでも黙らないか）を確かめる。

## 12. 決めたこと（2026-09-30）

| # | 決定 |
|---|---|
| 1 | この文書は `docs/development-workflow.ja.md`。`tests/TEST_PLAN.ja.md` は入口の一覧と実行手順に縮める。役目を終えた `test-strategy` / `harness-*` / `upload-and-fixture` / `infrastructure` は `docs/research/` へ移す（事実の記録として残す） |
| 2 | ベンチの定義は **(b)**: `.env` は個体と port（`TEST_SERIAL_PORT_<P>`、`TEST_BENCH_<P>`）、配線・期待する版・スロット・bind・plan・測った事実は `tests/benches/<name>.toml` を commit |
| 3 | ディレクトリは **(a)**: `unit / build / bench / manual`（+ データの `benches`、道具の `tools`） |
| 4 | プローブの firmware は **両方**: まず Release の tag から example をビルドして焼く（`prepare.py`）。oep-probe-arduino は Release にビルド済み（merged.bin / uf2 と sha256 の json）を workflow_run で付ける（本体のリリーススクリプトは共通なので変更しない。adc4943） |
| 5 | 旧 runner は **(a)**: 新しい `bench/` が同じ結果を出したら消す |
| 6 | probe-rs の残りは消す（公開 index にも入っていない） |
| 7 | DUT の part は錠にしない。家系の照合（`--chip`）で守る |
| 依存の版 | 下限だけ（`>=`）。固定はロックファイルの役目。互換のない版が出たら壊れて気づくのが好ましい |
| firmware の照合 | describe の `firmware`（tag 0x40）。oep-probe-arduino 0.0.5 から Release の版そのもの。「その版の Release の binary か」は prepare が焼くときに json の sha256 で確かめる |
