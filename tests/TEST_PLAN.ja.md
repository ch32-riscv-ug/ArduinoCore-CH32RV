# テスト計画

> English: [TEST_PLAN.md](TEST_PLAN.md)

対象: `ch32-riscv-ug:ch32v`(ArduinoCore-CH32)。
Arduino Board Manager経由で配布するArduino coreとして、**利用者が受け取るもの**が動くことを検証します。

> **この文書は提案です。** 特に「検証boardの絞り込み」はmaintainerの承認を経ていません
> ([承認状態 A-3](../docs/approval-status.ja.md))。実装済みのtoolやCIが動いていることは、
> ここに書いた方針が採用されたことを意味しません。

## テスト方針

テストはソフトウェアで環境を完全に制御できるかどうかで2種類に分けます。

**自動テスト**はCIまたはローカルで人の操作なしに実行します。
入力はすべてプログラムで生成し、期待される出力はすべてアサーションで検証します。

**手動テスト**は環境をソフトウェアで完全に制御できない場合に使います。
「自動化が面倒だから」ではなく、配線の変更・ロジックアナライザ・目視のように
**人の操作か専用計測器が本質的に必要**な場合に限ります。

この2つに加えて、このrepositoryには制約が3つあります。テスト設計はこの3つから決まっています。

| 制約 | 影響 |
|---|---|
| **Board Manager配布物である** | 検証対象はrepository treeではなく**配布アーカイブ**。install経路そのものがテスト対象 |
| **24 series / 122 part number** | 全部を実機では回せない。**ハードウェア差分軸**で代表を選ぶ |
| **WCH-LinkEを同時に1台しか接続できない** | 実機テストは**逐次**。常時接続するboardを絞る |

---

## Board Manager配布物としてのテスト

ライブラリやcoreをBoard Managerで配るときの典型的な失敗は、
**repositoryでは通るのにinstallすると壊れる**ことです。原因はほぼ次のどれかで、
どれも「repository treeでcompileする」テストでは絶対に検出できません。

| 失敗の型 | 具体例 | 対策 |
|---|---|---|
| アーカイブに入っていないファイルを参照している | `variants/`のld scriptを忘れる、`libraries/`が入っていない | `gen_index.py`の`PLATFORM_ENTRIES`を唯一の定義とし、**そこから作ったアーカイブでcompileする** |
| 開発時のpath上書きに依存している | `compiler.path=`が空(PATH探索)のまま出荷される | packagingで`{runtime.tools.…}`へ書き換え、**上書きを一切渡さずにcompile**する |
| ツールが降ってこない / 動かない | probe-rsが`.tar.xz`で、arduino-cliが展開できない | installして**実際に`--version`を実行**する |
| indexのmetadataが実体とずれる | boardが1個しか出ない、`version`がplatform.txtと違う | `boards`は`boards.txt`から、`version`は`platform.txt`から生成する |
| 古いversionがinstallできなくなる | index再生成で過去versionが消える | indexは**append-only**。`gen_index.py --merge` |
| 特定OSだけ壊れる | Windowsでsymlink、pathの区切り、`.exe` | install検証をlinux/macOS/Windowsのmatrixで回す |

### 検証マトリクス

| 配布物 | 何を確認するか | どこで |
|---|---|---|
| platform archive | 単一rootフォルダ、必須entryが揃う、checksum/size一致 | `tools/index/gen_index.py` |
| platform archive | **上書きなしでcompileが通る** | `tools/index/install_check.py` |
| package index | `version`が`platform.txt`と一致 | `gen_index.py`(不一致はエラー) |
| package index | `boards`が`boards.txt`の全24 boardと一致 | `gen_index.py` |
| package index | append-only(過去versionが残る) | `gen_index.py --merge` |
| toolchain tool | installされ、compilerとして解決される | `install_check.py` |
| probe-rs tool | installされ、`--version`が動く | `install_check.py` |
| upload経路 | `arduino-cli upload --programmer wch-link`が通る | `tests/manual/smoke/smoke.py`(実機) |
| profile経路 | `sketch.yaml`の`platform_index_url` + `programmer:`で動く | `tests/sketches/`(実機) |
| 3 OS | 上記すべて | `.github/workflows/ci.yml`の`install-test` matrix |

### 承認されていないもの

配布経路の実装は入っていますが、**外へ出す判断は未承認**です
([承認状態](../docs/approval-status.ja.md))。

- package index自体の公開。`release.yml`は未実行

### まだ埋まっていない穴

- [ ] **probe-rsは[ミラー](https://github.com/ch32-riscv-ug/mirror-probe-rs)経由**
      ([ADR-0011](../docs/adr/0011-tool-mirror-repository.ja.md))。
      ミラーが新versionを公開しても採用は手動なので、**採用時に認定を回す運用**が要る

- [ ] `libraries/`(SPI / Wire)を同梱したあと、installした状態で`#include <SPI.h>`が解決されるか

---

## テストの種類とディレクトリ

**`tests/`直下に置いてよいのは次の5種だけ**です。それ以外は必ずカテゴリの
ディレクトリへ入れます。`~/dev`配下の他プロジェクトと同じ規約です。

```text
tests/
  README.ja.md / README.md          この階層の使い方
  TEST_PLAN.ja.md / TEST_PLAN.md    本書
  conftest.py                       全カテゴリ共有のfixture
  pyproject.toml / uv.lock          実行環境
```

カテゴリのディレクトリは**pytestの入口とその補助スクリプトを同居**させます。
`test_<name>.py`が入口、隣にあるのがそれが呼ぶharnessです。

```text
tests/
  generated/   自動 — 生成物がch32-device-dataの表と一致するか
  vendor/      自動 — 取り込んだsnapshot(ArduinoCore-API / TinyUSB)の照合
  startup/     自動 — 統合crt0とEVT startupのELF等価性、割込みベクタ表
  compile/     自動 — 全122 part numberのcompile matrix、同梱examples(2段)、サイズ回帰
  sizebench/   自動 — newlibのサイズ計測
  package/     自動 — package index生成とBoard Managerからのclean install
  sketches/    自動 — sketch単位のAPIテスト(pytest + pytest-embedded)
  unit/        自動 — boardもbuildも要らない小さな検査
  manual/      手動 — 実機と人の操作が要るもの、および実機を扱う便利tool
```

| 入口 | 置き場所 | 何を見るか |
|---|---|---|
| `test_generated.py` | `generated/` | boards.txt / variant / vector includeが再生成と一致 |
| `test_vendored_api.py` | `vendor/` | ArduinoCore-API snapshotがlockのcommitと同一 |
| `test_vendored_tinyusb.py` | `vendor/` | TinyUSB snapshotが110ファイルのSHA-256と一致 |
| `test_startup_equivalence.py` | `startup/` | 自作crt0とEVT startupのELF等価性 |
| `test_interrupt_tables.py` | `startup/` | interrupts.csvがEVT startup assemblyと一致 |
| `test_compile_matrix.py` | `compile/` | 全part numberがcompileでき、サイズが基準線と一致 |
| `test_examples.py` | `compile/` | 同梱examplesが代表2枚(X035/V003)でcompileできる。**簡易段** |
| `test_examples_sweep.py` | `compile/` | 同梱examplesが**全24 series**でcompileできる。約20分、`--sweep`が要る。**GitHub段** |
| `test_sizebench.py` | `sizebench/` | newlibのサイズ計測harnessが動く |
| `test_package_install.py` | `package/` | 生成したindexからclean installできる |
| `test_sketch_profiles.py` | `sketches/` | 各sketch.yamlがboard一覧と同期 |
| `test_sketch_profile_build.py` | `sketches/` | profile経由(loopback index)でbuildできる |
| `test_board_layer.py` | `unit/` | boardレイヤの定義権限が守られているか。variantが`LED_BUILTIN`を定義しない、`Arduino.h`から`ch32_registers.h`へ到達しない、examplesが`LED_BUILTIN`をガードする、`requires:`が実在capabilityを指す([board-layer-rules](../docs/board-layer-rules.ja.md)) |
| `test_peripheral_table.py` | `unit/` | `docs/peripheral-support.ja.md`の○が、device-data由来のclock enableと矛盾しないか(片方向のみ。空欄は「EVTに例が無い」ことがあるので見ない) |
| `test_startup_parameters.py` | `unit/` | startupハーネスの`march`/`mabi`/startup定義が`boards.txt`の生成値と一致するか(二重管理の静かなズレを防ぐ) |
| `test_adc_instances.py` | `unit/` | ADC instanceを持つvariantで、instanceを持つpadにchannelもあるか。`A<n>`がADC1に留まっているか |
| `test_clock_prescaler.py` | `unit/` | AHB分周器の符号化表(compile時assertのみ) |
| `test_pd_frames.py` | `unit/` | USB PDのフレームロジック(hostのccで共有ライブラリにしてctypesで実行) |
| `test_tests_layout.py` | `unit/` | 本節の規約そのもの(下記) |

`sketches/`は**1 caseにつき1ディレクトリ**です。

```text
tests/sketches/<category>/<case>/
  <case>.ino
  sketch.yaml        profile = board。tests/sketches/sync_profiles.pyが生成
  testcmd.h          コマンド規約の雛形。sync_testcmd.pyが配る生成物
  test_<case>.py     1関数。バナーを待ち、コマンドを送り、順に読む
```

`sketches/`直下には、全caseで共有するものを置きます。

| ファイル | 役割 |
|---|---|
| `testcmd.h` | コマンド規約の雛形(原本)。各caseへコピーされる |
| `sync_testcmd.py` | `testcmd.h`の配布と`--check` |
| `sync_profiles.py` | `sketch.yaml`の`profiles:`生成と`--check` |
| `stage.py` | buildディレクトリへ何をコピーするか。3つのharnessが共有 |
| `compile_all.py` / `profile_build.py` | compile harness |

ホスト側にモジュールはありません。testは`dut`を直接使います——バナーを待ち、
コマンドを送り、順に読むだけなので、共有するものが無いからです。

`sketches/conftest.py`も置いていません。**pytestは全conftest.pyを`conftest`という
同じmodule名でimportする**ので、2つ目を置いた瞬間に`tests/conftest.py`が
sys.modulesから消えます。共有コードは`conftest.py`ではなく普通の名前のモジュール
([`loader.py`](loader.py))に置き、`from conftest import ...`は**1箇所もありません**
([`unit/test_tests_layout.py`](unit/test_tests_layout.py)が検査)。

`manual/`も**1 case = 1 ディレクトリ**で、`pytest`が入口です。`test_`プレフィックスは
**付けず**、`manual`は`norecursedirs`にも入れてあります——引数なしの`pytest`が実機を
焼きにいかないための二重の防護で、手動testは常にファイルを名指しして実行します。

```text
tests/manual/
  conftest.py              共有fixture(attached / bench / uart_routes)
  env_config.py            .envのpad名 -> pin番号、sketch用headerの生成
  bench.json               この作業台の配線記録
  <case>/<case>.py         手動テスト本体(必要なら<case>.ino + sketch.yaml)

  chip_info/       いま何が繋がっているか(probe / chip / port / FQBN / Serial pin)
  uart_scan/       boardがどのUSART routeを配線しているか特定する
  smoke/           出荷経路でcompile→upload→UART確認(Milestone 1のacceptance)
  crt0_probe/      自作crt0が渡すRAM(.data copy / .bss zero fill / .init_array)
  gpio_loopback/   ジャンパ1本でGPIO(レベル / pull / EXTI / PWM duty)
```

`chip_info` / `smoke` / `uart_scan`はCLIとしても動きます。対話的に作業台を見る場面の
ためで、どちらの経路も同じ関数を呼びます。設定は`tests/.env`の環境変数です
(pytestのoptionは増やしていません。`--port`と`--target`はpytest-embeddedのもの)。

`dist/`という名前は使いません。**pytest既定の`norecursedirs`に`dist`が入っている**ため、
`pyproject.toml`の除外リストを一行簡略化した日に収集対象から静かに消えます。
消えたTestは緑に見えるので、名前の側で避けます。

### `test_`プレフィックスの規約

**プレフィックスの有無が「引数なしの`pytest`が実行するか」の唯一のスイッチ**です。
ここを間違えると、どちらの向きでも緑のまま壊れます——実行されないTestは通ったように
見え、実行されるべきでないTestは実機を焼きます。

| 種類 | 名前 | 引数なし`pytest` | 例 |
|---|---|---|---|
| 自動Testの入口 | `test_<name>.py` | **実行する** | `compile/test_compile_matrix.py` |
| sketch caseの入口 | `test_<case>.py` | `--profile`が要る(下記) | `sketches/basic/wire_selftest/test_wire_selftest.py` |
| 手動Testの入口 | `<case>.py` | **実行しない** | `manual/smoke/smoke.py` |
| harness(本体) | `<name>.py` | 実行しない(Test関数を持たない) | `compile/compile_matrix.py` |

- **手動Testはプレフィックスを付けない**。中身は普通の`def test_*`関数なので、
  ファイルを名指しすれば`pytest`で走ります。名指しが要ることが安全装置です。
  加えて`manual`は`norecursedirs`にも入っています——二重にしてあるのは、片方だけだと
  「`pytest manual/`と打った瞬間に焼ける」「いつか誰かがファイル名を間違える」の
  どちらかが残るからです。
- **sketch caseは`test_`を付ける**。ただし`sketch.yaml`のあるディレクトリのTestは
  `--profile`が無ければ`tests/conftest.py`がskipにします。判定に使うのは
  *`sketch.yaml`が隣にあるか*で、`tests/sketches/`配下かどうかではありません
  ——`sketches/test_sketch_profiles.py`はcaseではなくcase一覧の検査だからです。
- **harnessは`test_`を付けない**。単体でも`uv run`できるscriptで、pytestからは
  `conftest.load()`が読み込みます。
- 同じファイル名を2箇所に置かない。pytestはTest moduleをフラットな名前空間に
  importするので、衝突は`import file mismatch`という分かりにくいエラーになります。

この規約自体は[`unit/test_tests_layout.py`](unit/test_tests_layout.py)が検査します。
新しいカテゴリを足すときは、そこの`CATEGORIES`と本節の表の両方に行を足してください。

---

## 検証boardの絞り込み(**提案・未承認**)

> 対象boardの確定は[Q-001](../docs/open-questions.ja.md)で未決です。
> どのboardを常時接続にするかは所有実機と運用の都合で決まるもので、
> ここに書いた階層は**そこから逆算した提案**にすぎません
> ([承認状態 A-3](../docs/approval-status.ja.md))。

24 series・122 part numberを全部実機で回すことはできません。
**compileは全部**、**実機はハードウェア差分軸の代表だけ**にします。

### ハードウェア差分軸

coreの実装が実際に分岐する軸だけを数えると6つです。

| 軸 | 値 | どこで効くか |
|---|---|---|
| ISA | `rv32ec` / `rv32emc` / `rv32imc` / `rv32imac` / `rv32imafc` | compiler、multilib、atomic有無 |
| GPIOポート幅 | 8 / 16 / 24 bit | `CFGLR`のみ / `+CFGHR` / `+CFGXR`・`BSXR` |
| SysTick | 32 / 64 bit | `millis()`のカウンタ読み出し |
| flash wait state | 0 / 1 / 2 | クロック引き上げ**前**に設定しないとハングする |
| vector table形式 | ジャンプ表 (V103) / アドレス表 | crt0とリンカ配置 |
| EXTIのvector分割 | `EXTI7_0`+`EXTI15_8` / `EXTI0..4`+`9_5`+`15_10` | `attachInterrupt()`のISR生成 |

### 階層

| Tier | Board | 実行頻度 | 選んだ理由 |
|---|---|---|---|
| **A** | **CH32X035** | 全PR相当(手元)＋release | 主対象と聞いている。24 bitポート・48 MHz・wait state 2という最も特殊な組み合わせ。USB-PDの実装先 |
| **A** | **CH32V003** | 同上 | 反対の極。`rv32ec`(M/A/Cなし)・8 bitポート・16 K flash・32 bit SysTick。ここが通れば下限が保証される |
| **B** | CH32V203 | 週次 / release前 | 最も普及しているV3系。`rv32imac` + 16 bitポート + 64 bit SysTick |
| **B** | CH32L103 | 週次 / release前 | V4C系。低消費電力系のクロック経路 |
| **B** | CH32V103 | 週次 / release前 | **vector tableがジャンプ表なのはこのfamilyだけ** |
| **B** | CH32V307 | 週次 / release前 | `rv32imafc`(F拡張)。手元で最大のpart |
| **C** | CH32V006 | 入手でき次第 | **1台で`rv32emc`とflash wait state 1の両方**を埋める。開発ボードが出ておりチップも販売中だが、本格的な利用はこれから |
| **D** | 残り17 series | compileのみ | 上のいずれかと差分軸が一致するか、**未発売で実機を用意できない** |

Tier A+Bで埋まる軸は4つです(GPIOポート幅 8/16/24、SysTick 32/64、vector table形式、
EXTIのvector分割)。残る2軸には**未踏の値があります**。

| 未踏の値 | 埋めるboard | 状況 |
|---|---|---|
| ISA `rv32emc` | CH32V006ほかV00x系 | Tier C。compiler側の差でcoreのCコードは同一 |
| ISA `rv32imc` | CH32V205 / CH32M030 | **未発売**。同上の理由で影響は小さい |
| **flash wait state 1** | CH32V006 / CH32V205 | Tier A+Bは0と2しか踏んでいない。**compileでは絶対に出ない軸**(クロック引き上げ前に設定しないとハングする)なので、実機で埋める価値がある |

したがって**CH32V006が実機に載るまで、wait state 1は未検証のまま**です。ISAの2値は
compilerの差なので`tests/compile`で足ります。

CH32V205とCH32X315は当初Tier Cに置いていましたが、**どちらも未発売**なので実行頻度を
持たせられません(2026-08-27にTier Dへ移動)。X315については「20 MHz/wait state 1」と
書いていましたが、boards.txtの実測値は`rv32imafc`・wait state 0で、**この記述は誤りでした**。
X315が固有に持つのは`x3x5`のvector tableで、発売されたら再検討します。

### examplesの2段構え(2026-08-29追加)

同梱examplesは**profileの対象ではありません**(実機で動かす約束ではない)。
compile側で2段に分けます。詳細は[examples-build-rules](../docs/examples-build-rules.ja.md)。

| 段 | 入口 | ボード | 目安 | いつ |
|---|---|---|---|---|
| 簡易 | `test_examples.py` | 代表2枚(X035/V003) | 約2分 | ローカル、コミット前 |
| sweep | `test_examples_sweep.py` | **全24 series**(`pnum=ANY`) | 約20分 | GitHub Actions専用job、Linuxのみ |

sweepは`--sweep`が要るopt-inです。どのseriesを対象にするかは
**exampleが自分の`.ino`で`requires:`宣言**し、`CH32_CLKEN_*`とboards.txtのANY容量から
解決します。CIに手書きのボード名はありません。

`sketch.yaml`のprofileはTier AとBにだけ作ります。
**profileがあるということは誰かが実機で回すという約束**なので、回せないprofileは無いほうがましです。
Tier C/Dは`tests/compile`のmatrixが見ます(profileは不要)。

Board追加時は[`tests/sketches/sync_profiles.py`](sketches/sync_profiles.py)の`BOARDS`だけを直し、
`uv run tests/sketches/sync_profiles.py`で全`sketch.yaml`を再生成します。

---

## 実機テストのコマンド規約

### なぜ必要か

**リセットの時刻はホストから見て不定です。** upload backendがいつコアを走らせるかは
probe種別・firmware版・chipで変わります。それなのに`setup()`で一度だけ出力して
黙るsketchは、**いつ始まるか分からない放送を聞きに行く**形になり、受信側を
どう工夫しても取りこぼします。

2026-08-22にCH32V103で実際に起きたことがこれです。9本すべてが
「前のsketchの出力を読む」状態になり(`heap_string`が`core_api`の行を読む)、
配線・firmware・probeの不調と3回誤診しました。受信側の対処を4通り試して
いずれも部分的にしか効いていません(詳細は[docs/todo.ja.md](../docs/todo.ja.md))。
**捨てるべき古い出力と本命が同じ経路を通る**以上、時間だけでは区別できません。

2台構成(ホスト役とデバイス役)では転送遅延が乗るため、この問題はさらに広がります。

### 規約

**ボードはバナーを繰り返し、聞かれるのを待つ。** `~/dev`配下の他プロジェクトと同じ形で、
理由も同じです——書き込みツールがボードをリセットし、**そのあとで**コンソールが開くので、
`setup()`の最初に印字したものは誰も聞いていないうちに終わっています。

```text
setup()   Serial.begin()だけ
loop()    tc_ready() → 0.5秒ごとに "<name> READY" を出し、
          コマンドが来たらその行を返す
```

**繰り返すことが要点**です。1回だけのバナーは「いつ始まるか分からない放送」ですが、
0.5秒ごとに出るバナーは**ホストが好きなときに待てば捕まえられる**ものになります。
1回だけの版で実際に起きたのが、CH32V103で9本すべてが前のsketchの出力を読んだ
一件です(`heap_string`が`core_api`の行を読む)。配線・firmware・probeの不調と
3回誤診しました。

もう一つの理由は、`setup()`が仕事をする場所として不適切なことです。20秒かかる
checkは「起動しないボード」と見分けがつきません。`RUN`とdone行の間なら「忙しい
ボード」に見えます。

**バナーはコマンドを受けたあとも止めません。** `~/dev`の参照実装は止めますが、
あちらはMCU内蔵のUSB-CDCで、こちらはWCH-LinkのUARTブリッジです。止める実装を
一度作って実測したところ、**ブリッジが行の途中で止まりました**——ホストには
`string=ab`まで届いて、あとはいくら待っても来ません。**バナーが管を動かしている**
ので、動かし続けます。

代償として、誰も読んでいないポートへボードが喋り続けるとブリッジが溢れ、
あとから出てくるものが混線します(`hooks_selftest READY`が`selftest READY`と
`hooY`に割れて、待っている文字列が連続して現れない)。ここはボードを黙らせるのでは
なく、**ホスト側がuploadを跨いでポートを開いたままにする**ことで解きます
([`manual/smoke/smoke.py`](manual/smoke/smoke.py))。

雛形は[`sketches/testcmd.h`](sketches/testcmd.h)です。arduino-cliはsketchフォルダの
外をコンパイルしないので、**各caseへコピーを配ります**([`sync_testcmd.py`](sketches/sync_testcmd.py)、
`--check`は`generated/test_generated.py`が回す)。`sketch.yaml`と同じく生成物です。

```cpp
#include "testcmd.h"

static void run_checks() {
  tc_check("nack_reported", rc != 0);     // "<name> PASS" / " FAIL"
  tc_checkv("millis", ok, elapsed);       // FAIL行に実測値を付ける
  tc_skip("mixed_route_refused", "one route only");   // boardが持たない機能
  tc_done();                              // "<name> done failures=N"
}

void setup() { tc_begin("wire_selftest"); }   // Serial.begin()だけ

void loop() {
  const char *cmd = tc_ready();           // バナーを繰り返し、コマンドを返す
  if (!cmd) return;
  if (!strcmp(cmd, "RUN")) run_checks();
  else tc_unknown(cmd);                   // 沈黙しない。歩調のずれを即座に返す
}
```

### ホスト側は「1 sketch = 1 テスト関数」

**1つのテスト関数の中で、順番に複数のcheckを読みます。** fixtureもconftestも要りません。

```python
def test_wire_selftest(dut) -> None:
    dut.expect_exact("wire_selftest READY", timeout=20)
    dut.write("RUN\n")
    dut.expect_exact("nack_reported PASS")
    dut.expect_exact("nack_bounded PASS")
    ...
    dut.expect_exact("wire_selftest done failures=0")
```

boardが持たない機能はtargetが`SKIP`を出すので、そこだけ
`dut.expect(r"tone_toggles_pin (PASS|SKIP .*)")`にします。**沈黙だけは許しません。**

**check 1つを1 testにはしません。** pytest-embeddedの`arduino_cli_build` /
`arduino_cli_upload`は**module scope**なのに`dut`は**function scope**で、
test関数ごとにportを開き直します。したがって
「前のtest関数が出させた行」を次のtest関数が読むことは**原理的にできません**。
分けたいなら、**test関数ごとに自分でコマンドを送って自分で読む**——
つまりcheckごとに個別コマンドを用意する——形にします。現状の12本は
`RUN`一発で足りるので、1関数にまとめてあります。

### 実装上の制約### 実装上の制約

**`String`を使いません。** CH32V003はRAMが2 KBで、`String`を含むsketchが載らないことは
`sketches/sync_profiles.py`の`REQUIREMENTS`で既に除外対象になっています。
行の受信は固定長バッファで行い、雛形(`tc_ready`)がそれを提供します
(`TC_CMD_MAX`、既定64バイト)。例外は`heap_string`だけで、そこでは`String`が
**被験体**です。

規約の導入でsketchは400〜500バイト増えます(`loop()`とコマンド解釈の分)。
**入らなくなったら`REQUIREMENTS`でboardを外すのではなく、caseを分割します。**
外すとそのboardのカバレッジが消えますが、分割すれば`sync_profiles.py`が
両方へ同じboard一覧を配るので**カバレッジは減りません**。

一度やりました。`core_api`がCH32V003で15972バイト(16 KBの97%)になったとき、
原因は**チェックの数ではなく1行**でした——`Serial.println(1.5, 2)`が**9428バイト**。
`Print::printFloat`が`double`を取り(ArduinoCore-API由来、ADR-0009で無改変)、
rv32ecにFPUが無いのでsoft-float一式(`__adddf3` 2346、`__subdf3` 2252、
`__divdf3` 1818、`__muldf3` 1510 ほか)が丸ごと入ります。
`print_format` caseへ出した結果:

| sketch | CH32V003 |
|---|---|
| `core_api`(分割後) | 6464バイト (39%) |
| `print_format`(新規) | 12772バイト (77%) |

**継ぎ目は「半分に割る」ではなく「高い機能」で選びます。** 半分に割っていたら
float側が同じ9.4 KBを抱えたまま、もう半分がスカスカになるだけでした。
`nm --size-sort -S` でどのsymbolが効いているかを先に見てください。

`~/dev`配下の他プロジェクトは同じ規約を`String`ベースで実装していますが、
そちらの下限はUno級でRAMに余裕があります。**規約は共通、実装だけ我々の下限に合わせます。**

---

## ペリフェラル別の検証方法

コストの安い順に4つあります。**上から順に検討し、下は上で無理なときだけ使います。**

| # | 方法 | コスト | 何が分かるか | 何が分からないか |
|---|---|---|---|---|
| 1 | **セルフチェック** — MCUが自分で確かめて結果をSerialに出す | ほぼゼロ。配線不要 | APIが値を返す、範囲に入る、ハングしない | 端子に実際に何が出ているか |
| 2 | **ボード内loopback** — 同じboardの2 pinをジャンパで直結 | ジャンパ線1本 | 出力が入力として観測できる。実際の信号経路 | 波形の精度(周波数・duty・立ち上がり) |
| 3 | **デバッガによる状態確認** — probe-rsでレジスタ/RAMを読む | 配線ゼロ(既に繋がっている) | 周辺レジスタが期待値になっているか | 端子まで出ているか、タイミング |
| 4 | **ロジックアナライザ** | 高い。1台ずつ | 波形の精度そのもの | — |

方法3はこのrepositoryではまだ使っていませんが、**配線をまったく増やさずに済む唯一の方法**なので、
全boardにロジアナを繋げない以上、Tier B以下では2の代わりに使う価値があります。
`probe-rs read b32 <addr> <n>`で周辺レジスタが読めます
(例: USART1の`CTLR1` = `0x40013800 + 0x0C`)。

### 機能別の割り当て

| 機能 | 方法 | 必要な配線 | 現状 |
|---|---|---|---|
| Serial 送信 | 2 (probeのUART bridgeへ) | TX → probe RX | ✅ `smoke.py` / `serial_println` |
| Serial 受信 | 2 | RX ← probe TX | ✅ `serial_echo`(bench 6枚すべて実機PASS) |
| Serial ボーレート精度 | 4 | ロジアナをTXへ | ⬜ |
| `millis()` / `micros()` | 1(hostの経過時間と突き合わせ) | 不要 | ✅ `core_api` |
| `digitalWrite` / `digitalRead` | 1(出力pinは入力経路にも入る) | 不要 | ✅ `core_api` |
| `digitalRead` の入力・プルアップ | 2(出力pin → 入力pin) | ジャンパ1本 | 🔧 `manual/gpio_loopback`(実機未実行) |
| `analogRead` | 1(範囲チェック)、2(既知電圧をGND/VDDから) | ジャンパ1本 | ✅ 範囲のみ |
| `analogWrite` (PWM) duty | 2(PWM出力 → 別pinで`pulseIn`) | ジャンパ1本 | 🔧 `manual/gpio_loopback`(実機未実行) |
| `analogWrite` (PWM) 周波数 | 4 | ロジアナ | ⬜ |
| `attachInterrupt` | 1(自分の出力エッジを拾う) | 不要 | ✅ `core_api` |
| `attachInterrupt` の他ポート | 2 | ジャンパ1本 | 🔧 `manual/gpio_loopback`(実機未実行) |
| `shiftOut` / `shiftIn` | 2(2 pinを直結) | ジャンパ1本 | 🔧 ハングしないことのみ |
| `pulseIn` | 2(PWM出力を測る) | ジャンパ1本 | 🔧 timeoutのみ |
| ヒープ (`String` / `malloc`) | 1 | 不要 | ✅ `heap_string` |
| `printf` / stdio | 1 | TXのみ | ✅ `stdio_printf` |
| SPI | 2(MOSI → MISO 直結でloopback) | ジャンパ1本 | ⬜ 未実装 |
| SPI mode / clock | 4 | ロジアナ | ⬜ 未実装 |
| Wire (I2C) | 2(同一board上のslave役 or EEPROM) | 外部device | ⬜ 未実装 |
| USB-PD (X035) | 4 + 実negotiation | 専用治具 | ⬜ 未実装 |
| クロック設定 | 3(RCCレジスタ読み出し)、1(`millis()`の歩度) | 不要 | 🔧 間接的 |
| 割り込み優先度 / PFIC | 3 | 不要 | ⬜ |

凡例: ✅ 実装済み / 🔧 部分的 / ⬜ 未着手

### なぜ全boardにロジアナを繋げないか

ロジアナは1台をboard間で差し替えるしかなく、差し替えのたびに人が要ります。
したがって**方法4はTier Aの2 boardに限定**し、Tier B以下は方法1〜3で見ます。
波形の精度はboardではなく**周辺回路のIP**で決まるため、
同じIPを持つfamilyの代表1台で測れば十分という判断です。

---

## WCH-LinkEを1台しか繋げない制約

この環境では複数のWCH-LinkEを同時に使えないことが分かっています(USBIPが2台目を拒否)。
実機テストは逐次になります。選択肢は次の4つです。

| 案 | 内容 | 評価 |
|---|---|---|
| a | 人が差し替える(現状) | Tier Aだけなら現実的。CIには乗らない |
| b | 別ホスト(Raspberry Pi等)をrunnerにしてboard farmを置く | 本命。USBIPの制約を回避でき、self-hosted runnerにできる |
| c | セルフホストGitHub Actions runner | bの延長。まずbが要る |
| d | Tier Aを常時、Tier Bはrelease前に差し替え | 追加投資なしで今すぐできる |

**当面はd**とし、bを別途準備します。
dの前提として、**どのboardが繋がっているかをスクリプトが自分で判定できる**必要があるので、
`tests/manual/chip_info/chip_info.py`がprobeとchipを問い合わせてFQBNとSerial pinまで出します。
`smoke.py`も`--board`と実チップが食い違えばフラッシュ前に落とします。

---

## テストカバレッジ一覧

| 機能 | 自動 | 手動 | 未カバー |
|------|------|------|---------|
| 全part numberのcompile | ✅ `compile`(122 part number、3 OS) | | |
| ELFサイズ回帰 | ✅ `compile/check_sizes.py` | | ⬜ RAM上限に対する警告 |
| crt0 / vector tableの正しさ | ✅ `startup`(EVT startupとのELF等価性、14 variant) | | |
| 生成物の同期 | ✅ `generated-sync`(device-dataから再生成してdiff) | | |
| vendored ArduinoCore-API の同期 | ✅ `api-sync` | | |
| Board Manager install → compile | ✅ `install-test`(3 OS、上書きなし) | | ⬜ upgrade経路、`libraries/`解決 |
| probe-rsのinstallと起動 | ✅ `install-test` | | |
| Serial 送信 | ✅ `serial_println`(V003 / V103 / V203 / X035 / L103 / V307 実機PASS) | ✅ `smoke.py` | |
| Serial 受信 | ✅ `serial_echo`(V003 / V103 / V203 / X035 / L103 / V307 実機PASS) | ✅ `smoke.py` | ⬜ フロー制御、エラーフラグ |
| ヒープ(`String`/`malloc`/`free`/OOM) | ✅ `heap_string`(V003 / V103 / V203 / X035 / L103 / V307 実機PASS) | ✅ `smoke.py` | ⬜ 断片化、`realloc` |
| ビルドmenu(`pnum` / `printf`) | ✅ `compile_all.py`(pnum)、実機(printf) | | ⬜ 全menu組み合わせのcompile |
| `printf` / stdio | ✅ `stdio_printf` / `print_format`(V003 / V103 / V203 / X035 / L103 / V307 実機PASS) | ✅ `smoke.py` | ⬜ float書式、`nano.specs`未適用でsketchが約40 KB膨らむ |
| 時間 (`millis`/`micros`/`delay`) | ✅ `core_api` | | ⬜ 長時間のオーバーフロー、歩度精度 |
| GPIO | ✅ `core_api`(出力の読み戻し) | | ⬜ 入力プルアップ、他ポートへのloopback |
| ADC | ✅ `core_api`(範囲) | | ⬜ 既知電圧での確度、分解能設定、複数ch |
| PWM | 🔧 `core_api`(ハングしないこと) | | ⬜ duty、周波数、pin ↔ timer対応 |
| 外部割り込み | ✅ `core_api`(自エッジ) | | ⬜ 他ポート、CHANGE/LOW、優先度 |
| `shiftOut` / `pulseIn` | 🔧 `core_api`(ハングしないこと) | | ⬜ 実波形 |
| 乱数 / 数学 | ✅ `core_api` | | |
| USART route自動判定 | | ✅ `uart_scan.py` | |
| チップ / probe判定 | | ✅ `chip_info.py` | |
| SPI | | | ⬜ **未実装** |
| Wire (I2C) | | | ⬜ **未実装** |
| USB (X035 PD / V307 HS) | | | ⬜ **未実装** |
| 低消費電力モード | | | ⬜ **未実装** |
| SDI printf(WCH-Link経由のdebug出力) | | | ⬜ **未実装**(別Serialクラスとして検討) |

---

## 実行方法

```sh
uv run tools/index/fetch_tools.py     # toolchain / probe-rs / device-data を .tools/ へ
cd tests && uv sync
```

`arduino-cli`がPATHに必要です。それ以外は`<repo>/.tools`に入るので、
**環境変数の設定は要りません**。版は`tools/index/tools_*.json`(package indexの正本)
から取るため、**利用者がinstallするのと同じ版**でtestが回ります。
詳細は[tests/README.ja.md](README.ja.md)。

Windowsでは作業ディレクトリを`<repoのドライブ>:\ch32t\`以下に作ります。`%TEMP%`配下
(`C:\Users\<user>\AppData\Local\Temp\pytest-of-…`)は80文字あり、Board Manager
installテストが入れるtoolchainがMAX_PATH(260)を超えます。GCCはinclude pathを
`bin/../lib/gcc/…/../../../../riscv-none-elf/include/c++/…`と**解決せずに**開くため、
`bits/c++config.h: No such file or directory`という、実在するfileが無いという形で落ちます。
別の場所にしたいときは`CH32_TEST_TMP`を指定。作業ディレクトリはsession終了時に消します
(`CH32_KEEP_TMP=1`で残す)。

### 自動テスト(実機なし)

**すべて`pytest`ひとつです。**

```sh
cd tests
uv run pytest                  # 実機もprofileも要らないもの全部(約4分)
uv run pytest -m "not slow"    # compile系を飛ばす(数秒)
```

内訳と個別実行は[tests/README.ja.md](README.ja.md)。
sketchテストをbuildだけで回すなら`--profile ch32x035 --run-mode build`。

### 自動テスト(実機あり)

```sh
cd tests
uv run pytest manual/chip_info/chip_info.py -v -s     # まず何が繋がっているか確認
uv run --env-file .env pytest sketches --profile ch32x035 --port /dev/ttyACM4
```

### 手動テスト

```sh
cd tests
uv run --env-file .env pytest manual/chip_info/chip_info.py -v -s   # 作業台の前提確認
uv run --env-file .env pytest manual/smoke/smoke.py -v -s           # 受け入れsketch
CH32_SKETCH=all uv run --env-file .env pytest manual/smoke/smoke.py -v -s   # 差し替え後
uv run --env-file .env pytest manual/uart_scan/uart_scan.py -v -s   # 配線が不明なとき
uv run --env-file .env pytest manual/crt0_probe/crt0_probe.py -v -s  # boardを載せ替えたら
uv run --env-file .env pytest manual/gpio_loopback/gpio_loopback.py -v -s   # ジャンパ要
```

`--board`は省略できます。probe-rsが型番を読んで`boards.txt`から逆引きするので、
**焼く対象と焼く相手が食い違うことが原理的に起きません**。
明示すると検出結果に対する主張になり、食い違えば止まります。

作業台固有の値(手動testが使うpad等)は`tests/.env`で上書きします
([`.env.example`](.env.example)が説明)。

手動テストは常に`-s`を付けます。オペレータへの指示が端末に出ます。

---

## 関連文書

- [テスト戦略](../docs/test-strategy.ja.md) — レイヤー分けとsource of truthの考え方
- [環境整備計画](../docs/infrastructure.ja.md) — W-1〜W-7のworkstream
- [tests/README.ja.md](README.ja.md) — profileと開発中platformの関係、既知のarduino-cliの不具合
- [tests/manual/README.ja.md](manual/README.ja.md) — 作業台の配線とboardごとのSerial pin
