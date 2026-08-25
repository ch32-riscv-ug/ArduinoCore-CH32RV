# 承認状態(実装済み・未承認の一覧)

文書基準日: 2026-08-25

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
| A-4 | **`smoke.py`/`uart_scan.py`の`--board`を省略可能にし、probe-rsの検出結果を既定にする** | `tests/manual/smoke/smoke.py`、`uart_scan.py` | X035実機で自動判定・明示一致・明示不一致(exit 1)・`--pnum detect`の4経路を確認 | 既定の挙動変更。`[compile only]`のseriesは検出できないため`--board`必須のまま。CIでどちらを使うか未決 | ⬜ |
| A-6 | **32 bitタイマのレジスタ幅をvariantが宣言し、`ATRLR`を幅に応じて書き分ける** | `tools/generate/generate.py`(`WIDE_TIMERS`)、`cores/arduino/ch32_registers.h`、`wiring_tone.cpp`、`libraries/Servo/src/Servo.cpp` | CH32L103実機。修正前はTIM4が`cnt_high_5ms=65517`/割り込み0、修正後は`7994`/5 ms 5回。`tone_selftest` 9/9 pass、L103全体12/12 pass。16 bit側(V103)に退行が無いことも実機で確認 | **バグ修正そのものより、事実の置き場所が未決。** どのfamilyが32 bitタイマを持つかを`generate.py`へ手書きしている(device-dataに機械可読な表がまだ無いため)。上流に表を作ってもらってデータ由来にするか、手書きのまま`UNUSABLE_PADS`と同じ扱いにするか。また`CH32X035`の`CHnCVR`だけがunionになっている件(PWM duty)は未確認 | ⬜ |
| A-7 | **一対多のpad解決を可視化する**(挙動は不変) | `tools/generate/generate.py`(`load_pin_routes`の`alts`、`gen_pins`の`alternatives()`、`main()`の要約) | `b1285de`で**193組**(af-N 183 / default 10 / remap-N 0)を検出。`--check`はコメント追加のみで6 variant、pinの値は1つも動かない | 上流の助言(2026-08-25)は「衝突検出ではなく、どれを選んだかを残せ」。残す形をコメントにしたのが妥当かは未決。**選び方そのものは手つかず**で、いまも「表の最後」。素朴な規則(対応型番数→若い番号)はSWD padを選んでしまうので入れていない | ⬜ |
| A-8 | **USBPDライブラリ(sink)のAPIの形とフレームロジック** | `libraries/USBPD/`(`pd_frames.c/h`、`USBPD.h/cpp`、example、README×2、keywords)、`tests/unit/test_pd_frames.py`、`tests/sketches/basic/pd_selftest/` | host 14 test + 実機18 check(CH32V103、failures=0)。V003(31%)/X035にcompile可。examplesの全compileも緑 | API命名(`USBPD`インスタンス、`request`/`requestProfile`/`maintain`)と設計判断(固定優先・丸めない・battery/variable列挙のみ)は**私の提案**で未承認。ハードウェアドライバ未実装(`begin()`はfalse)。変異体のdefine生成は155c398取り込み待ち | ⬜ |

## 承認されたもの

| # | 内容 | 承認 | 日付 |
|---|---|---|---|
| A-5 | **probe-rsを[`mirror-probe-rs`](https://github.com/ch32-riscv-ug/mirror-probe-rs)経由で参照する** | [ADR-0011](adr/0011-tool-mirror-repository.ja.md) `Accepted`。releaseを公開し、そこからのclean installが通ることを確認したうえでmaintainerが承認 | 2026-08-20 |

## 却下されたもの

| # | 内容 | 却下理由 | 日付 |
|---|---|---|---|
| A-2 | probe-rsのWindowsアーカイブを再パッケージして本repositoryで再ホストする | **手動publishになるため他のtoolとversionがずれる。運用が持たない。**[ADR-0002](adr/0002-toolchain-distribution.ja.md)の「再ホストしない」方針にも反する。実装(`repack_probe_rs.py`、`publish-tool.yml`)は撤去済み | 2026-08-20 |

代わりに[ADR-0011](adr/0011-tool-mirror-repository.ja.md)(`Accepted`)の
**tool配布専用repositoryでの自動ミラー**を採用しました(A-5)。A-2との違いは、
publishが自動でversionが機械的に追従することと、コア本体のrelease streamに
混ざらないことです。**Windowsのinstallは解決済み**です。

調査結果は[R-18](research/probe-rs-archive-layout.ja.md)。

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
| [`mirror-probe-rs`の`update.yml`](https://github.com/ch32-riscv-ug/mirror-probe-rs) | 日次 + 手動dispatch | **稼働中**。v0.32.0を公開済み。以降は自動で追従する(採用は手動) |

## 関連

- [ADR一覧と承認プロセス](adr/README.ja.md)
- [未決定事項](open-questions.ja.md)
- [引継ぎメモ](handoff.ja.md)
- [TODO](todo.ja.md)
