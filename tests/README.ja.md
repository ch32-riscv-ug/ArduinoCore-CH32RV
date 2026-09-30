# tests

**まず[テスト計画](TEST_PLAN.ja.md)を読んでください。** 層の切り方と入口の一覧、実行手順がそこにあります。方針と決定は
[docs/development-workflow.ja.md](../docs/development-workflow.ja.md)。ここは道具の在処と、つまずきやすい点です。

## セットアップ

```sh
uv run tools/index/fetch_tools.py     # toolchain / device-data を <repo>/.tools へ
cd tests && uv sync                   # Python 3.13、pytest-embedded-arduino-cli(-ch32rv)、pytest-embedded-wireskein
cp .env.example .env                  # このベンチの port と bench file。任意
```

`arduino-cli` が PATH に要ります。それ以外は `<repo>/.tools` に入り、環境変数の設定は要りません。

| | |
|---|---|
| 置き場所 | `<repo>/.tools/<name>/<version>/`（gitignore 済み） |
| 版の正本 | [`tools/index/tools_*.json`](../tools/index/)。package index を作るのと同じファイルなので、**利用者が install するのと同じ版**で test が回る |
| 完全性 | ダウンロードは展開前に SHA-256 を照合 |
| device-data | `vendor/ch32-device-data.lock.toml` の locked commit を checkout |
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
uv run --env-file .env bench/prepare.py --profile ch32x035           # 全消去 → Release の firmware を焼く → 設定 → 照合
```

X035 ジグは P4 の USB-Serial/JTAG から焼くので `TEST_BENCH_CH32X035_UPLOAD`、WSL では焼くたびに HS の口が usbipd から外れるので
`TEST_BENCH_CH32X035_USBIP_BUSID` も要ります（[.env.example](.env.example)）。

### つまずきやすい点

- **`sketch.yaml` の profile は index から入れた platform しか解決しません。** 作業ツリーの symlink では tool の依存が解けません。
  ベンチも利用者と同じ「公開 index から入れる」形で、profile が pin する版は `platform.txt` と同じ（`sync_profiles.py` が読む）。
  リリース前の検査だけ `tools/index/install_check.py` が loopback の index を使います。
- **console は UART ではなく debug module（`SerialDMSeq`、dmseq）です。** ch32rv の monitor が読みます。UART は試験対象で、どの USART が
  probe に届いているかは bench file の `[uart]` が言い、test が `UART <n> <route> <baud>` で sketch に指名します。
- **console の往復は ch32rv 0.12.2 で 13〜26 ms、0.12.3 で 2 ms**（monitor の 20 ms の待ちが直った）。trace 試験の capture の窓は
  0.12.2 でも通るように広く取ってあり、probe の capture に trigger が付いたら詰めます。
- **probe の plan は自分が組んだ fn だけ release します。** 全 release は保存済みの fixture UART の plan まで消していました
  （oep-probe-arduino 0.0.6 から仕様で守られる）。
- **ベンチ上の作業は同時に 1 つ。** 並行に焼く・回すと WSL が落ちたことがあります。
- Windows では作業ディレクトリを `<drive>:\ch32t\` 以下に作ります。`%TEMP%` の深さで toolchain の include path が MAX_PATH を超えます
  （`CH32_TEST_TMP` で変更、`CH32_KEEP_TMP=1` で残す）。
