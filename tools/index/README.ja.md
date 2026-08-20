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
| `tools_probe_rs.json` | probe-rs 0.32.0のtool定義(6 host)。Windowsだけ再ホスト**案**(未承認)、他はupstream直リンク |
| `probe_rs_targets.csv` | probe-rsが知っているCH32 target 127件。`{build.probe_rs_chip}`の生成元 |
| `fetch_tools.py` | **testに要るものを`<repo>/.tools`へ揃える**(toolchain / probe-rs / device-data tables)。版とchecksumは`tools_*.json`が正本 |
| `toolenv.sh` | `CH32_*`が未設定なら`.tools`の場所を入れるshell helper。設定済みのものは触らない |
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
# toolchainのアーカイブは .tools/cache から取る(fetch_tools.pyが置く)
./test_install.sh /tmp/w5

# probe-rsもローカルから配信して完全オフラインにする
CH32_PROBE_RS_ARCHIVE=/path/to/probe-rs-tools-<target>.tar.xz ./test_install.sh /tmp/w5
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

## probe-rsは`mirror-probe-rs`経由で参照している

indexのprobe-rs entryは、upstreamではなく
[`ch32-riscv-ug/mirror-probe-rs`](https://github.com/ch32-riscv-ug/mirror-probe-rs)を
指しています([ADR-0011](../../docs/adr/0011-tool-mirror-repository.ja.md))。

### 理由

arduino-cliは**tool archiveが単一のroot directoryを持つこと**を要求します。
probe-rsの配布物はそれをOSによって満たしたり満たさなかったりします。

| host | 形式 | root directory | arduino-cli |
|---|---|---|:--:|
| Linux / macOS | `.tar.xz` | `probe-rs-tools-<triple>/` あり | ✅ |
| Windows | `.zip` | **無し**(7ファイルがroot直下) | ❌ |

これはprobe-rs固有ではなく、リリースを作っている
[cargo-dist](https://github.com/axodotdev/cargo-dist)の**意図的な規約**です。

> The "root" of an archive is either the actual root directory of the archive
> (zips); or a directory with the same name as the archive, but without the
> extension (tarballs). This difference is for compatibility/legacy reasons…
> — [cargo-dist book, Archive Contents](https://github.com/axodotdev/cargo-dist/blob/main/book/src/artifacts/archives.md)

同じ要求の緩和は[arduino-cli#325](https://github.com/arduino/arduino-cli/issues/325)が
`conclusion: declined`で閉じており、**indexからWindows entryを削っても回避できません**
(entryの無いhostは`no versions available for the current OS`でinstallごと失敗する。
実験で確認)。

### ミラーが何をしているか

- 平坦なアーカイブだけroot directoryを付けて詰め直す。**判定は実物の検査**なので、
  upstreamが直せば自動的に素通しへ戻る
- それ以外は**バイト単位でそのまま**再配布する(checksumがupstream公開値と一致)
- upstreamのURLとchecksumを記録し、来歴を追えるようにする
- 詰め直しは**決定的**。誰でもupstreamから同じchecksumを再現できる

### versionの採用

`tools_probe_rs.json`は**ミラーのreleaseから取ってきた写し**です。手編集しません。
ミラーが新versionを公開しても、このファイルを更新するまで利用者への影響はありません。
更新は認定matrix通過を条件にします([ADR-0002](../../docs/adr/0002-toolchain-distribution.ja.md))。

`gen_index.py`は`upstream*`と`repacked`を落としてからindexへ入れます
(来歴はfragmentの読み手向けで、Board Managerのschemaではないため)。

### 現状

**解決済み**(2026-08-20)。ミラーはprobe-rs 0.32.0を公開しており、
CIの`install-test`は3 OSすべてで回っています。

公開後に確認したこと:

- 公開資産6 hostすべてのchecksum/sizeがfragmentの記載と一致
- 素通し4種のchecksumがupstream公開値と一致
- 詰め直したWindowsアーカイブの中身がupstreamとバイト一致(7ファイル、差分なし)
- **ミラーURLから実際にダウンロードするclean installが通る**

## 検証済みの重要事実

- arduino-cliはtool archiveの単一rootフォルダを平坦化し、`{runtime.tools.<name>.path}`が
  `bin/`直上を指す → **xPackアーカイブを再パッケージなしでtool参照できる**(STM32duino方式の追認)
- tool定義はpackager配下(`ch32-riscv-ug:xpack-riscv-none-elf-gcc`)で解決される
- `.tar.xz`のtool archiveをarduino-cliは展開できる(probe-rsで実証)
- **tool archiveは単一のroot directoryを持たなければならない**。平坦なものは
  `files in archive must be placed in a subdirectory`で拒否される。
  平坦な`.zip`とroot付きの`.zip`を実際に作って両方installし、確認済み
- **そのhost向けentryが無いtoolも、installを失敗させる**
  (`no versions available for the current OS`)。「対応しないhostは書かない」では逃げられない
- `core upgrade`後も、同じindexから古いversionを`@0.0.1`指定でinstallできる(append-onlyの実証)

## 今後

- win/mac/linux-arm64 hostでの同検証はGitHub Actions matrixで実施中
- `libraries/`(SPI / Wire)同梱後、installした状態で`#include <SPI.h>`が解決されるかの確認
