# 実験0009: 非シリアル書き込みのarduino-cli経路とpytest harness

実施日: 2026-08-19
対象question: Q-044(uploader frontend)、Q-016(host/自動test方式)、Q-041(probe選択)
実施環境: WSL2 Linux x86_64、arduino-cli 1.3.1、xPack riscv-none-elf-gcc 14.3.0-1、実機なし

## 目的

CH32の書き込みはWCH-LinkE(SWD)でシリアルではない。Arduinoの標準経路(`arduino-cli upload`)と、
他プロジェクトで使っている`pytest-embedded-arduino-cli`が、この非シリアル経路を扱えるかを実装前に確定させる。

## 結果1: arduino-cliは非シリアル書き込みを標準機能で扱える

`programmers.txt`と`tools.<t>.program.pattern`(`{serial.port}`を参照しない)を用意して実測。

| # | 実行 | 結果 |
|---|---|---|
| 1 | `upload`(port/programmerなし) | ✗ `A programmer is required to upload` |
| 2 | `upload --programmer X`(portなし) | **✓** |
| 3 | `sketch.yaml` profileに`programmer:`を書き`upload --profile` | **✓** flag不要 |
| 4 | 3 + `--port /dev/ttyACM0`を併用 | **✓** programmer経路を維持し`New upload port: /dev/ttyACM0 (serial)` |
| 5 | `upload --build-path ... --profile ...`(plugin生成形) | **✓** |

実験4が要点。**pytest pluginがruntime serial用に`--port`を渡してもprogrammer経路は維持される**ため、
「書き込み=LinkE(SWD)、Serial=LinkE内蔵UART」という2経路構成がそのまま成立する。

`upload.pattern`(portが要る)ではなく**`program.pattern`**を使うことが条件。

## 結果2: pytest-embedded-arduino-cliの拡張は不要

pluginの`ArduinoCliUploadConfig.upload_command()`は
`arduino-cli upload --build-path <p> [--profile <x>] [--port <p>] <sketch>`を生成する。
`--profile`が渡るので、profile内の`programmer:`がそのまま効く。

なお`extra_args`フィールドは存在するがCLI optionからもsketch.yamlからも供給されていない
(`plugin.py`は`port=`のみ渡す)。`--programmer`をflagで渡したい場合は拡張が要るが、
profile経由で足りるため**現時点で拡張は不要**。

## 結果3: profileはindex経由installのplatformしか解決できない

- profileの`platforms:`に記載したplatformを、arduino-cliが`~/.arduino15/internal/<name>_<ver>_<hash>/`へ
  展開して使う。**`~/.arduino15/packages/`は変化しない**
- したがってprofileを使う限り、利用者の通常のArduino環境を汚さない([R-15](../research/local-install-and-test-env.ja.md)方式Aのsymlinkより隔離性が高い)
- 逆に、**方式A(repoを`<sketchbook>/hardware/`へsymlink)はprofileでは使えない**。
  ローカル開発でもローカルindex配信(方式B)が要る

### arduino-cli 1.3.1のbug

profileに`platforms:`が無いと、エラーではなく**panicする**。

```text
panic: runtime error: index out of range [0] with length 0
  internal/arduino/sketch.(*Profile).RequireSystemInstalledPlatform(...)
      internal/arduino/sketch/profiles.go:125
```

upstreamへ報告する。

## 結果4: pytest harnessが成立することを確認

`tests/`に他プロジェクトと同じ構成(`pytest` + `pytest-embedded` + `pytest-embedded-serial` +
`pytest-embedded-arduino-cli` + `pytest-html`)を用意し、
`tests/sketches/basic/serial_println/`をmilestone 1の受け入れtestとして作成。

ローカルindexを立てて`--run-mode build`で実行し、pluginがprofileを解決し
`arduino-cli compile --profile ch32v00x`まで到達することを確認した。

現在の失敗は想定どおりで、コアがまだスタブであることを正しく検出している。

```text
error: 'Serial' was not declared in this scope
error: 'HEX' was not declared in this scope
```

## 結論

- Q-044について: **書き込みfrontendを独自binaryにする理由は「arduino-cliが非シリアルを扱えないから」ではない**。
  標準の`programmers.txt`+`program.pattern`で足りる。独自tool開発の根拠はprobe選択(Q-041)とbackend gapに限定される
- Q-016について: 自動test基盤は`pytest-embedded-arduino-cli`単体で成立する。ArduTestは使わない
- profileがboard切り替えの単位になる(1 board = 1 profile)
- 配布物に`programmers.txt`が必要。`tools/index/gen_index.py`の`PLATFORM_ENTRIES`へ追加済み

## 再現手順(要点)

```sh
python3 tools/index/gen_index.py --platform . --out /tmp/idx \
    --base-url http://127.0.0.1:8781 --version 0.0.1
(cd /tmp/idx && python3 -m http.server 8781) &
# sketch.yaml の platform_index_url を localhost へ向ける
cd tests && uv sync
uv run pytest sketches/basic/serial_println --profile ch32v00x --run-mode build
```
