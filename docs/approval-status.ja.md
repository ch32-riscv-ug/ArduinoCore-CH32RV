# 承認状態(実装済み・未承認の一覧)

文書基準日: 2026-08-20

## この文書の役割

**動くこと**と**採用が決まっていること**は別です。
[ADR](adr/README.ja.md)は`Proposed`のまま実装が進む場合があり、
実測裏付けは「その選択肢が成立する」ことの証明であって採用の承認ではありません
(2026-08-19にADR-0001〜0008が`Accepted`から`Proposed`へ戻された経緯も同じ理由)。

そのため、**実装が入っているが承認されていないもの**をここに一覧します。
コードやCIが緑であることと、この表の`承認`列が埋まることは無関係です。

## 原則

| | |
|---|---|
| 実装 | 検証のために作ってよい。まず実測し、結果を報告する |
| 文書 | **確定として書かない。** 提案であることと、未決の論点を明示する |
| リリース・外部公開 | **承認が出てから。** package indexの公開、GitHub Releaseへの資産添付、外部への配布はすべてこれに当たる |

## 実装済み・未承認

| # | 内容 | 実装場所 | 実測 | 未決の論点 | 承認 |
|---|---|---|---|---|:--:|
| A-1 | **`--specs=nano.specs`を既定にし、`%f`を`menu.printf`のopt-inにする** | `platform.txt`、`boards.txt`(生成) | printf sketchが48,492→7,064 byte(X035)。CH32V003(16K)にも載るようになる。X035実機で`none`/`float`両方確認 | [ADR-0004](adr/0004-runtime-and-cxx.ja.md)は**`Proposed`**。同ADRはnano既定と`%f` opt-inを提案しているが承認されていない。menuの文言、`-u _printf_float`かmenuかの選択、size baselineへの影響 | ⬜ |
| A-3 | **検証boardをTier A/B/C/Dへ絞る** | [tests/TEST_PLAN.ja.md](../tests/TEST_PLAN.ja.md) | ハードウェア差分6軸を数え、Tier A+BでISA以外の全軸を踏むことを確認 | Q-001(対象boardの確定)が未決。どのboardを常時接続にするかは所有実機と運用の問題で、私が決められない | ⬜ |
| A-5 | **probe-rsを`mirror-probe-rs`経由で参照する** | `tools/index/tools_probe_rs.json` | 詰め直したWindowsアーカイブがarduino-cliでinstallできること、中身がupstreamとバイト一致すること、決定的であること、差し替え検知が働くことを確認 | 方針は[ADR-0011](adr/0011-tool-mirror-repository.ja.md)で承認済み。**ミラーのreleaseが未公開**のためURLは現時点で404。公開後にCIの`install-test`へwindows-latestを戻す | 🔄 |
| A-4 | **`smoke.py`/`uart_scan.py`の`--board`を省略可能にし、probe-rsの検出結果を既定にする** | `tests/manual/smoke.py`、`uart_scan.py` | X035実機で自動判定・明示一致・明示不一致(exit 1)・`--pnum detect`の4経路を確認 | 既定の挙動変更。`[compile only]`のseriesは検出できないため`--board`必須のまま。CIでどちらを使うか未決 | ⬜ |

## 却下されたもの

| # | 内容 | 却下理由 | 日付 |
|---|---|---|---|
| A-2 | probe-rsのWindowsアーカイブを再パッケージして本repositoryで再ホストする | **手動publishになるため他のtoolとversionがずれる。運用が持たない。**[ADR-0002](adr/0002-toolchain-distribution.ja.md)の「再ホストしない」方針にも反する。実装(`repack_probe_rs.py`、`publish-tool.yml`)は撤去済み | 2026-08-20 |

代わりの方針として、**tool配布専用repositoryでの自動ミラー**を
[ADR-0011](adr/0011-tool-mirror-repository.ja.md)にまとめ、
方針はmaintainer承認済みです(2026-08-20)。却下されたA-2との違いは、
publishが自動でversionが機械的に追従することと、コア本体のrelease streamに
混ざらないことです。実体は[`ch32-riscv-ug/mirror-probe-rs`](https://github.com/ch32-riscv-ug/mirror-probe-rs)。

調査結果と他の選択肢は
[tools/index/README.ja.md](../tools/index/README.ja.md)「Windowsでinstallできない」と
[research/probe-rs-archive-layout.ja.md](research/probe-rs-archive-layout.ja.md)に
まとめてあります。**現状Windowsではinstallできません**(Linux/macOSは通ります)。

## 承認されたら消えるもの

承認時は次をまとめて行い、この表から行を消します。

1. 該当ADRの`Status`を`Accepted`にする(ADRが無いものは先に起こす)
2. 各文書の「提案」表記を外す
3. [open-questions](open-questions.ja.md)の該当Qを閉じる
4. 外部公開が絡むものは、承認後にworkflowを実行する

## 外部へ出るものの現状

いずれも**未実行**で、人が明示的に起動しない限り動きません。

| workflow | 起動条件 | 状態 |
|---|---|---|
| [`release.yml`](../.github/workflows/release.yml) | tag `v<version>`のpush(手動dispatchではbuildのみ) | 未実行。package indexは未公開 |
| [`mirror-probe-rs`の`update.yml`](https://github.com/ch32-riscv-ug/mirror-probe-rs) | 日次 + 手動dispatch | **未実行**。最初のreleaseを作るまでindexのURLは404 |

## 関連

- [ADR一覧と承認プロセス](adr/README.ja.md)
- [未決定事項](open-questions.ja.md)
- [引継ぎメモ](handoff.ja.md)
- [TODO](todo.ja.md)
