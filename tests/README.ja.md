# tests

**まず[テスト計画](TEST_PLAN.ja.md)を読んでください。** 層の切り方と入口の一覧、実行手順がそこにあります。方針と決定は
[docs/development-workflow.ja.md](../docs/development-workflow.ja.md)。ここは道具の在処と、つまずきやすい点です。

## セットアップ

```sh
uv run tools/index/fetch_tools.py     # toolchain / device-data を <repo>/.tools へ
uv run tests/bench/install_tools.py   # ベンチ用: その toolchain と ch32rv を arduino-cli の packages/ へ link（bump_tool.py の後も）
cd tests && uv sync                   # Python 3.13、pytest-embedded-arduino-cli(-ch32rv)、pytest-embedded-wireskein
cp .env.example .env                  # このベンチの port と bench file。任意
```

`arduino-cli` が PATH に要ります。それ以外は `<repo>/.tools` に入り、環境変数の設定は要りません。

| | |
|---|---|
| 置き場所 | `<repo>/.tools/<name>/<version>/`（gitignore 済み） |
| 版の正本 | [`tools/index/tools_*.json`](../tools/index/)。package index を作るのと同じファイルなので、**利用者が install するのと同じ版**で test が回る |
| 完全性 | ダウンロードは展開前に SHA-256 を照合 |
| device-data | `vendor/ch32-device-data.lock.toml` の commit を checkout し、`index/manifest.csv` の sha256 と `index/VERSION` を lock と照合。読むのは公開面 `index/` だけ |
| 上書き | `CH32_GCC_BIN` / `CH32_TABLES` が設定済みならそちら（`loader.py`） |

`.tools/cache` は取ってきたアーカイブの置き場で、消しても取り直すだけです（約 400 MB）。EVT mirror（`CH32_MIRROR_ROOT`）だけは
`.tools` に入れていません（startup 等価性の test だけが使う大きな clone）。

## 実行

```sh
cd tests
uv run pytest                                   # unit + build（約 7 分）
uv run pytest -m "not slow"                     # compile 系を飛ばす（数秒）
uv run pytest --clean                           # cache を消してから
uv run --env-file .env pytest bench --profile ch32x035          # 常設ベンチ 1 台の全部
uv run --env-file .env pytest bench/basic/serial_println --profile ch32v203 -s
```

`--clean` は pytest-embedded-arduino-cli の option（`arduino-cli compile --clean`）で、`conftest.py` がそれに乗って `.pytest_cache`
と scratch も消します。`.tools` と `~/.arduino15` は消しません（消しても結果は変わらず、1 時間伸びるだけ）。

harness はすべて Python module で、pytest は `loader.load()` で読んで関数を呼びます。単独でも `uv run` できます
（shell script は 1 本もありません。Windows で 3 回壊れたのでやめました）。

## ベンチ

`.env`（commit しない）にはこの machine の port と bench file の名前だけを書きます。配線・期待するプローブの版・スロット・bind・
plan・測った事実は `benches/<name>.toml`（治具の設計、commit する）にあります。

```sh
TEST_SERIAL_PORT_CH32X035=oep://30eda0e31108-hs/x035     # IDE port（wchlink://<serial> でも）
TEST_BENCH_CH32X035=x035-p4                                # benches/x035-p4.toml
```

```sh
uv run --env-file .env bench/prepare.py --profile ch32x035 --check   # プローブが bench file と合っているか（pytest も最初にこれをする）
uv run --env-file .env bench/prepare.py --profile ch32x035           # Release の firmware を焼く → 設定を消して書く → 照合
uv run --env-file .env bench/prepare.py --profile ch32x035 --usj     # 空のプローブ: bootloader の口から全消去して焼く
```

firmware の入れ方は bench file の `[probe] update`。`"dfu"`（P4、oep-probe-arduino 0.0.16 以降）は Release の app image を
**HS の口の USB DFU** で送る（`bench/dfu.py`、dfu-util 不要。動いている firmware が検証して反対の面に書き、再起動。壊れた image は
errVERIFY で断られて今の firmware のまま）。正式ベンチの P4 は HS しか出さないのでこれが常用。既定の `"usj"` は tag からビルドして
bootloader の口（`TEST_BENCH_<P>_UPLOAD`）から全消去して焼く — 空のプローブの初回はこちら（`--usj`）。WSL では焼くたびに HS の口が
usbipd から外れるので `TEST_BENCH_CH32X035_USBIP_BUSID` も要ります（[.env.example](.env.example)）。

### つまずきやすい点

- **bench は作業ツリーを焼きます（Release ではなく）。** bench の `sketch.yaml` の profile は platform を**版無し**で書き
  （`- platform: ch32-riscv-ug:ch32rv`）、`bench/conftest.py` がセッションの間だけこのリポジトリを
  `<sketchbook>/hardware/ch32-riscv-ug/ch32rv` に symlink します（host-arduino-core と同じ形）。toolchain と ch32rv は
  `{runtime.tools.*.path}` で引かれるので、`bench/install_tools.py` が `tools_*.json` の版を `<data>/packages/ch32-riscv-ug/tools/`
  へ link しておきます（pytest が最初に照合し、無ければそのコマンドを案内して止まる）。**自分の platform を Board Manager で
  入れてはいけません**: 同じ platform が 2 つあると arduino-cli は版の高いほうを取り、Release が作業ツリーの代わりに焼かれます
  （`--check` が見つけます）。外部（プローブの esp32 platform、ライブラリ）は版 pin の Release のみ。同梱 examples の profile は
  利用者向けなので版 pin + 公開 index のまま（`profile_build.py` がそれを loopback の index で確かめる）。Release の archive
  そのものの検査は `tools/index/install_check.py`。
- **console は UART ではなく debug module（`SerialDMSeq`、dmseq）です。** ch32rv の monitor が読みます。UART は試験対象で、どの USART が
  probe に届いているかは bench file の `[uart]` が言い、test が `UART <n> <route> <baud>` で sketch に指名します。
- **console の往復は ch32rv 0.12.2 で 13〜26 ms、0.12.3 で 2 ms**（monitor の 20 ms の待ちが直った）。trace 試験の capture の窓は
  0.12.2 でも通るように広く取ってあり、probe の capture に trigger が付いたら詰めます。
- **probe の plan は自分が組んだ fn だけ release します。** 全 release は保存済みの fixture UART の plan まで消していました
  （oep-probe-arduino 0.0.6 から仕様で守られる）。
- **ベンチ上の作業は同時に 1 つ。** 並行に焼く・回すと WSL が落ちたことがあります。
- Windows では作業ディレクトリを `<drive>:\ch32t\` 以下に作ります。`%TEMP%` の深さで toolchain の include path が MAX_PATH を超えます
  （`CH32_TEST_TMP` で変更、`CH32_KEEP_TMP=1` で残す）。
