# tests

**まず[テスト計画](TEST_PLAN.ja.md)を読んでください。** 自動/手動の切り分け、
検証boardの階層、ペリフェラル別の検証方法、Board Manager配布物としての検証項目が
そこにまとまっています。ここはその実行手順です。

## 実行

**すべて`pytest`ひとつで回ります。**

```sh
cd tests
uv run pytest                                   # boardもprofileも要らないもの全部(約4分)
uv run pytest -m "not slow"                     # compile系を飛ばす(数秒)
uv run pytest --profile ch32x035 --run-mode build   # + sketch testをbuildだけ
uv run pytest --profile ch32x035 --port /dev/ttyACM4  # + 実機で実行
```

| test | 内容 | 実機 | marker |
|---|---|---|---|
| `test_generated.py` | 生成物とsketch profileがtablesと同期しているか | 不要 | |
| `test_vendored_api.py` | `cores/arduino/api`がpin先とバイト一致 | 不要 | |
| `test_interrupt_tables.py` | 割込み表がEVT startupと一致 | 不要 | EVT mirror要 |
| `test_startup_equivalence.py` | 統合crt0とEVT startupのELF等価性(14 variant) | 不要 | `slow`、EVT mirror要 |
| `test_compile_matrix.py` | 全122 part numberのcompile + sizeベースライン | 不要 | `slow` |
| `test_sketch_profiles.py` | 全sketch × 全profile boardのcompile | 不要 | `slow` |
| `test_sizebench.py` | newlibのサイズ計測(nano vs full) | 不要 | `slow` |
| `test_package_install.py` | Board Manager install → 上書きなしcompile → upgrade/rollback | 不要 | `slow` |
| [`sketches/`](sketches/) | Arduino APIのsketch単位test | 任意 | `--profile`必須 |
| [`manual/`](manual/README.ja.md) | 手動test + 実機tool | 必要 | 明示指定のみ |

`sketches/`は`--profile`が無いとskipします(profileが無いとpytest-embeddedが
targetを決められないため)。`manual/`は`test_`プレフィックスを付けていないので、
ファイルを名指ししない限り収集されません。

中身のshell harness(`test_compile.sh`等)はそのまま残してあり、pytestはそれを呼んで
出力のmarkerを検証します。単独でも動きます。

| ディレクトリ | 内容 | 単独実行 |
|---|---|---|
| [`sketches/`](sketches/) | sketch単位test | `uv run pytest sketches/...` |
| [`manual/`](manual/README.ja.md) | 手動test + 実機tool | `uv run pytest manual/<case>/<case>.py -v -s` |
| [`compile/`](compile/README.ja.md) | compile matrix + size baseline | `test_compile.sh <workdir>` |
| [`startup/`](startup/README.ja.md) | crt0等価性 | `run_check.sh <workdir>` |
| [`sizebench/`](sizebench/README.ja.md) | newlibサイズ計測 | `run_sizebench.sh <workdir>` |

## sketches/

`pytest-embedded-arduino-cli`(+`pytest-embedded`)を使います。他プロジェクトと同じ構成で、
**1 caseにつき1 sketchディレクトリ**、`sketch.yaml`のprofileで対象boardを切り替えます。

```text
sketches/<category>/<case>/
  sketch.yaml        profile定義(board = profile)
  <case>.ino
  test_<case>.py     dut fixtureへのexpect
```

`sketch.yaml`のprofile一覧は**生成物**です。boardを増減するときは
[`sketches/sync_profiles.py`](sketches/sync_profiles.py)の`BOARDS`だけを直します。

```sh
uv run tests/sketches/sync_profiles.py           # 全sketch.yamlを再生成
uv run tests/sketches/sync_profiles.py --check   # CI: 古ければ失敗
CH32_GCC_BIN=<xpack>/bin tests/sketches/compile_all.sh /tmp/sk   # 全組み合わせをcompile
```

sketchによっては小さいboardに載りません(`String`はCH32V003の2 KB RAMに入らない、
newlibのフルprintfは約40 KB)。その下限は`sync_profiles.py`の`REQUIREMENTS`に書き、
入らないboardはそのsketchのprofileから外します。`compile_all.sh`が全組み合わせを
実際にcompileするので、**載らないboardをprofileが名乗ることはできません**。

### セットアップ

clone直後にこれだけです。

```sh
uv run tools/index/fetch_tools.py     # toolchain / probe-rs / device-data を .tools/ へ
cd tests && uv sync
cp .env.example .env                  # 作業台固有の設定(手動testのpin等)。任意
```

`arduino-cli`がPATHに必要です。それ以外は`<repo>/.tools`に入り、**環境変数の設定は不要**です。

#### ツールがどこから来るか

| | |
|---|---|
| 置き場所 | `<repo>/.tools/<name>/<version>/`(gitignore済み) |
| 版の正本 | [`tools/index/tools_*.json`](../tools/index/)。package indexを作るのと同じファイルなので、**利用者がinstallするのと同じ版**でtestが回る |
| 完全性 | ダウンロードは展開前にSHA-256を照合 |
| device-data | `boards.txt`が記録しているlocked commitでcheckoutする |
| probe-rs | [`mirror-probe-rs`](https://github.com/ch32-riscv-ug/mirror-probe-rs)から([ADR-0011](../docs/adr/0011-tool-mirror-repository.ja.md)) |

環境変数(`CH32_GCC_BIN` / `CH32_PROBE_RS` / `CH32_TABLES` / `CH32_XPACK_ARCHIVE`)は
**上書き用として残してあります**。設定済みならそちらが優先されるので、
作業台に別の場所へ置いた物があっても使えます。

`.tools/cache`はダウンロードしたアーカイブの置き場です。消しても次回取り直すだけです
(約400MB)。

EVT mirror(`CH32_MIRROR_ROOT`)だけは`.tools`に入れていません。他repositoryの
大きなcloneで、[startup等価性test](startup/README.ja.md)しか使わないためです。

`.env`は**作業台ごとの設定**です。手動testが使うpadのように、どのboardが載っているかで
変わる値を置きます。既定値はどれも作者の作業台で動く値なので、未設定でもエラーにはなりません。

```sh
uv run --env-file .env pytest manual/gpio_loopback/gpio_loopback.py -v -s
```

`.env`はgitignoreされていて、[`.env.example`](.env.example)が唯一の説明です。

### 実行

```sh
# 実機なし(CIが回す形)。buildが通ることだけを見る
uv run pytest sketches --profile ch32v00x --run-mode build

# 実機あり
uv run pytest sketches --profile ch32v00x --port /dev/ttyACM0
```

### profileと開発中platformの関係(重要)

`sketch.yaml`のprofileは**index経由でinstallされたplatformしか解決できません**。
[R-15](../docs/research/local-install-and-test-env.ja.md)の方式A(repoを
`<sketchbook>/hardware/`へsymlink)はprofileでは使えないため、ローカル開発では
方式B(ローカルindex配信)を使います。

```sh
python3 tools/index/gen_index.py --platform . --out /tmp/idx \
    --base-url http://127.0.0.1:8781 --tools local
(cd /tmp/idx && python3 -m http.server 8781)
```

`sketch.yaml`の`platform_index_url`をこのURLへ向けてください。

**arduino-cli 1.3.1の既知の不具合**: profileに`platforms`が無いと、エラーではなく
panicします(`internal/arduino/sketch/profiles.go:125`)。

### 書き込み経路(非シリアル)

CH32の書き込みはWCH-LinkE(SWD)で、シリアルではありません。arduino-cli 1.3.1で
次を実測確認済みです。

- `programmers.txt` + `platform.txt`の`tools.<t>.program.pattern`(`{serial.port}`非参照)を用意し、
  profileに`programmer:`を書けば、`arduino-cli upload --profile`は`--port`なしで通る
- pytest pluginがruntime serial用に`--port`を渡しても、programmer経路が維持される

したがって**pluginの拡張は不要**です。programmerの実体はQ-040/Q-044の決定待ちのため、
`sketch.yaml`の`programmer:`は現在コメントアウトしています。
