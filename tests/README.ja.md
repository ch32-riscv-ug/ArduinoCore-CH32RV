# tests

| ディレクトリ | 内容 | 実機 | 実行 |
|---|---|---|---|
| [`sketches/`](sketches/) | Arduino API のsketch単位test(pytest) | 任意 | `uv run pytest sketches/...` |
| [`compile/`](compile/README.ja.md) | 全SKU compile matrix + size baseline | 不要 | `test_compile.sh <workdir>` |
| [`startup/`](startup/README.ja.md) | 統合crt0とEVT startupのELF等価性 | 不要 | `run_check.sh <workdir>` |
| [`sizebench/`](sizebench/README.ja.md) | newlibサイズ計測 | 不要 | `run_sizebench.sh <workdir>` |

## sketches/

`pytest-embedded-arduino-cli`(+`pytest-embedded`)を使います。他プロジェクトと同じ構成で、
**1 caseにつき1 sketchディレクトリ**、`sketch.yaml`のprofileで対象boardを切り替えます。

```text
sketches/<category>/<case>/
  sketch.yaml        profile定義(board = profile)
  <case>.ino
  test_<case>.py     dut fixtureへのexpect
```

### セットアップ

```sh
cd tests
uv sync
```

`arduino-cli`がPATHに必要です。

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
