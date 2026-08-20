# Board Manager配布: package index生成とinstall検証

関連: [テスト計画](../../tests/TEST_PLAN.ja.md)「Board Manager配布物としてのテスト」、
[環境整備計画](../../docs/infrastructure.ja.md) W-5、
[R-15方式B](../../docs/research/local-install-and-test-env.ja.md)、
[R-04](../../docs/research/toolchain-distributions.ja.md)、
[実験0005](../../docs/experiments/0005-package-index-install.ja.md)

## 目的

**利用者が受け取るものを検証する。** repository treeでcompileが通ることと、
Board Manager経由でinstallしたplatformでcompileが通ることは別物です。
ここのscriptは後者を、公開サーバなしでローカル再現します。

## 構成

| ファイル | 内容 |
|---|---|
| `gen_index.py` | platformを`.tar.bz2`化し、`package_ch32-riscv-ug_index.json`を生成 |
| `tools_xpack_gcc.json` | xPack GCC 14.3.0-1のtool定義(6 host、GitHub Releases直リンク、公式`.sha`由来のchecksum) |
| `tools_probe_rs.json` | probe-rs 0.32.0のtool定義(6 host)。**Windowsだけ再ホスト**、他はupstream直リンク |
| `repack_probe_rs.py` | probe-rs Windows zipをroot directory付きへ再パッケージ(決定的・checksum検証つき) |
| `probe_rs_targets.csv` | probe-rsが知っているCH32 target 127件。`{build.probe_rs_chip}`の生成元 |
| `fetch_xpack.py` | xPackアーカイブの取得(CIのcache用) |
| `test_install.sh` | ローカルHTTP配信 → 新規data dirへclean install → **上書きなしでcompile** → upgrade/rollback |

publishは[`.github/workflows/release.yml`](../../.github/workflows/release.yml)が行います
(tag `v<version>` push で Release + GitHub Pages)。

## gen_index.pyがindexへ入れるもの

| 項目 | 出どころ | 理由 |
|---|---|---|
| `version` | `platform.txt`の`version=` | indexとplatform.txtがずれると、installされたversionとIDE表示が食い違う。`--version`で違う値を渡すとエラー |
| `boards` | `boards.txt`の`<ID>.name=`(24件) | Board ManagerのUIに出る一覧。手書きだと必ず腐る |
| `url` / `checksum` / `size` | 生成したアーカイブの実測 | — |
| tool依存 | `tools_*.json` | toolchainとprobe-rsの両方 |
| 過去version | `--merge <既存index>` | **indexはappend-only**。消すとpinしている利用者がinstallできなくなる |

packaging時に`platform.txt`の`compiler.path=`を
`{runtime.tools.xpack-riscv-none-elf-gcc.path}/bin/`へ書き換えます
(作業treeはsymlink開発用にPATH探索の既定のまま)。

アーカイブに入るのは`PLATFORM_ENTRIES`のallowlistだけです
(`platform.txt` / `boards.txt` / `programmers.txt` / `cores` / `variants` /
`libraries` / `bootloaders` / `system`)。`tests`や`docs`は入りません。

## 使い方

```sh
# ローカル検証(toolchainだけローカル、probe-rsはGitHubから)
CH32_XPACK_ARCHIVE=/path/to/xpack-riscv-none-elf-gcc-14.3.0-1-linux-x64.tar.gz \
./test_install.sh /tmp/w5

# 完全オフライン(probe-rsもローカルに置く)
CH32_XPACK_ARCHIVE=... CH32_PROBE_RS_ARCHIVE=/path/to/probe-rs-tools-<target>.tar.xz \
./test_install.sh /tmp/w5
```

最後に`INSTALL-AND-COMPILE OK`が出れば通っています。途中の確認点:

```text
Sketch uses 772 bytes  ...  Blink (CH32V006) が上書きなしでcompileできた
Sketch uses 2776 bytes ...  受け入れsketch (CH32X035) がcompileできた
ARCHIVE CONTENTS OK    ...  tests/docs/tools が混入していない
PROBE-RS INSTALL OK    ...  .tar.xz が展開され、バイナリが起動した
UPGRADE AND ROLLBACK OK...  0.0.2へupgradeし、0.0.1を再installできた
```

本番形のindexを手で作るとき:

```sh
uv run --no-project python gen_index.py --platform ../.. --out dist \
  --base-url https://github.com/ch32-riscv-ug/ArduinoCore-CH32/releases/download/v0.0.1 \
  --tools github --merge <公開中のindex.json>
```

## Windowsだけprobe-rsを再ホストしている理由

arduino-cliは**tool archiveのファイルがroot直下にあると拒否します**。

```text
Cannot install tool ch32-riscv-ug:probe-rs@0.32.0:
  searching package root dir: files in archive must be placed in a subdirectory
```

probe-rsのLinux/macOS向け`.tar.xz`は`probe-rs-tools-<triple>/`というroot directoryを
持つのでそのまま通りますが、**Windows向け`.zip`は平坦**(7ファイルがroot直下)です。
upstreamにroot付きのWindowsアーカイブは存在しません。

そこでこの1アーカイブだけ、root directoryを付けて再パッケージし、本repositoryの
releaseへ添付します([ADR-0002](../../docs/adr/0002-toolchain-distribution.ja.md)の
「再ホストしない」方針の例外)。

- `repack_probe_rs.py`は**決定的**です(entry順固定・timestamp固定・圧縮方式固定)。
  同じupstreamからは必ず同じchecksumが出るので、indexがpinしているSHA-256を
  誰でも再現・検証できます
- upstreamのURLとchecksumは`upstreamUrl` / `upstreamChecksum`として
  `tools_probe_rs.json`に残します(publish前にダウンロードを検証する。
  indexへは出しません)
- probe-rsはMIT/Apache-2.0で、両方のLICENSEファイルがアーカイブ内に同梱されています
- 中身はupstreamとバイト単位で同一です(7ファイルすべてSHA-256一致を確認)

publishは[`publish-tool.yml`](../../.github/workflows/publish-tool.yml)。
probe-rsのversionを上げるたびに1回、platformのreleaseより先に実行します。
CIのWindows install-testは**ローカルで再パッケージしたものを配信して検証**するので、
publish前でもレイアウト崩れはPRで落ちます。

```sh
uv run tools/index/repack_probe_rs.py --out dist          # 作るだけ
uv run tools/index/repack_probe_rs.py --out dist --update   --base-url https://github.com/ch32-riscv-ug/ArduinoCore-CH32/releases/download/tools-probe-rs-0.32.0
```

## 検証済みの重要事実

- arduino-cliはtool archiveの単一rootフォルダを平坦化し、`{runtime.tools.<name>.path}`が
  `bin/`直上を指す → **xPackアーカイブを再パッケージなしでtool参照できる**(STM32duino方式の追認)
- tool定義はpackager配下(`ch32-riscv-ug:xpack-riscv-none-elf-gcc`)で解決される
- `.tar.xz`のtool archiveをarduino-cliは展開できる(probe-rsで実証)
- **tool archiveは単一のroot directoryを持たなければならない**。平坦なものは
  `files in archive must be placed in a subdirectory`で拒否される。
  平坦な`.zip`とroot付きの`.zip`を実際に作って両方installし、確認済み
- `core upgrade`後も、同じindexから古いversionを`@0.0.1`指定でinstallできる(append-onlyの実証)

## 今後

- win/mac/linux-arm64 hostでの同検証はGitHub Actions matrixで実施中
- `libraries/`(SPI / Wire)同梱後、installした状態で`#include <SPI.h>`が解決されるかの確認
