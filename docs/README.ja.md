# 初期設計ドキュメント

文書基準日: 2026-08-17

このディレクトリは、旧コアの調査結果と新コアの初期設計案を、次の開発セッションへ引き継ぐためのものです。

## 文書の状態

各文書では、内容を次の3種類に分けます。

- **決定済み**: プロジェクト開始時点で合意済みの事項
- **提案**: 現在有力だが、ADRまたは実測を経て決定する事項
- **確認済み事実**: ソース、実装、一次資料などで確認した事項

「提案」を実装上の確定仕様として扱わないでください。重要な決定は[`docs/adr/`](adr/README.ja.md)へ記録します。

## 言語とファイル名

- 日本語文書は`*.ja.md`とする
- 利用者向けに内容が固まった文書は、英語版を`*.md`として用意する
- 英語版がまだ必要ない初期調査・設計文書は、日本語版だけでよい
- 現時点で英語版を持つ利用者向け文書はトップの`README.md`だけである
- 翻訳版を追加した場合は、相互に言語切替linkを置く

## 読む順序

1. [handoff.ja.md](handoff.ja.md) — 現在地と次の着手点
2. [project-scope.ja.md](project-scope.ja.md) — 目的、非目的、対応の定義
3. [architecture.ja.md](architecture.ja.md) — コア全体の境界案
4. [open-questions.ja.md](open-questions.ja.md) — 次に決める論点
- [TODO(未対応作業)](todo.ja.md): 簡略化のたびに積み上げる先送り作業の一覧
- [承認状態](approval-status.ja.md): 実装は入っているが承認されていないもの。**外部公開はここが埋まってから**
5. 作業内容に応じて以下の各文書

## 文書一覧

| 文書 | 内容 |
|---|---|
| [handoff.ja.md](handoff.ja.md) | 新しいスレッド／担当者向けの要約 |
| [research/README.ja.md](research/README.ja.md) | 事前調査(startup、EVT構造、SKU/board構造、toolchain、テスト環境) |
| [infrastructure.ja.md](infrastructure.ja.md) | 環境整備計画(実機なしworkstream、repository分離、GitHub Actions/Pages) |
| [experiments/](experiments/0001-xpack-multilib-smoke.ja.md) | 実験記録(0001〜。toolchain、startup等価性、platform、generator、index install、newlibサイズ) |
| [project-scope.ja.md](project-scope.ja.md) | プロジェクト目標、初期スコープ、非目標 |
| [architecture.ja.md](architecture.ja.md) | Arduino API、内部HAL、SoC、EVT互換の境界 |
| [device-data.ja.md](device-data.ja.md) | 独立device databaseの配置、repository境界、Arduino consumer方針 |
| [legacy-audit.ja.md](legacy-audit.ja.md) | 旧リポジトリの構造、問題、継承すべき知見 |
| [ecosystem.ja.md](ecosystem.ja.md) | Arduino、WCH、ch32fun、書き込みツールの調査 |
| [vendor-policy.ja.md](vendor-policy.ja.md) | 外部ソースの固定、取込、patch、ライセンス方針 |
| [toolchain.ja.md](toolchain.ja.md) | toolchainの候補、選定条件、認定matrix |
| [flash-size.ja.md](flash-size.ja.md) | 何がフラッシュを食うか、map fileの読み方、削り方 |
| [upload-and-fixture.ja.md](upload-and-fixture.ja.md) | uploader、WCH-Link識別、実機fixture |
| [test-strategy.ja.md](test-strategy.ja.md) | unit、host、compile、HIL、logic analyzer、CI |
| [board-layer-rules.ja.md](board-layer-rules.ja.md) | どの層(series/SKU/board/sketch)が何を定義してよいか。`LED_BUILTIN`とSKU maskの扱い |
| [examples-build-rules.ja.md](examples-build-rules.ja.md) | examplesのビルド対象宣言、capabilityによるスキップ、簡易テストとsweepの分離 |
| [software-peripherals.ja.md](software-peripherals.ja.md) | bit-bangで実装できるペリフェラルの調査と仕様(SoftSPI/SoftWire/SoftSerial/OneWire) |
| [roadmap.ja.md](roadmap.ja.md) | 段階的な実装順と完了条件 |
| [open-questions.ja.md](open-questions.ja.md) | 未決定事項、必要な実験、判断基準 |
| [approval-status.ja.md](approval-status.ja.md) | **実装済みだが承認されていないもの**の一覧。動くことと採用が決まっていることは別 |
| [adr/README.ja.md](adr/README.ja.md) | ADR一覧(0001: device data独立repo、0002: toolchain=xPack直リンク、0003: owned startup/vector/linker、0004: newlib-nano+GNU++17、0005: family board+pnum生成、0006: RTOSはベアメタル+将来ライブラリ、0007: extra_flagsユーザー専用、0008: 書き込みdefault=WCH-LinkE、0011: tool配布を別repositoryで自動ミラー) |

## 更新規則

- 外部プロジェクトのversionや状態には確認日を付ける
- 対応デバイス一覧を文書へ重複して手書きしない。将来はmanifestから生成する
- 実測していない事項は「未検証」と明記する
- 方針変更時は、古い記述を静かに上書きせずADRまたは履歴を残す
- ローカル環境だけで通用するパスやUSB列挙番号を恒久的な識別子として記録しない
