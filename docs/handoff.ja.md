# 引継ぎメモ

文書基準日: 2026-08-19

## 現在地

`ArduinoCore-CH32`は、旧[`arduino_core_ch32_riscv_noneos`](https://github.com/ch32-riscv-ug/arduino_core_ch32_riscv_noneos)を修復せず、長期保守を前提に新規設計するプロジェクトです。対象は全CH32ファミリ(11 family / 27 series / 103型番)。

実機なしフェーズ(環境整備)は完了済みです。リポジトリには設計文書、[事前調査](research/README.ja.md)、[実験記録0001〜0008](experiments/0001-xpack-multilib-smoke.ja.md)、[ADR-0001〜0005](adr/README.ja.md)、検証済みprototype([統合startup](../prototypes/startup/README.ja.md)、[最小platform](../prototypes/platform/README.ja.md)、[generator](../prototypes/generator/README.ja.md)、[index/install](../prototypes/index/README.ja.md)、[sizebench](../prototypes/sizebench/README.ja.md))と、3 OSでall greenの[CI](../.github/workflows/ci.yml)があります。**コア本体(実API)の実装はこれから**です。

device databaseは独立[`ch32-device-data`](https://github.com/ch32-riscv-ug/ch32-device-data)が正本([ADR-0001](adr/0001-device-data-repository.ja.md)、[境界](device-data.ja.md))。

## 決定済み

ADRになっている決定:

- [ADR-0001](adr/0001-device-data-repository.ja.md): device databaseは独立repository。Arduino coreは固定versionのconsumer
- [ADR-0002](adr/0002-toolchain-distribution.ja.md): default toolchainは**xPack riscv-none-elf-gccのGitHub Releases直リンク参照**(候補14.3.0-1)。WCH forkは比較lane限定
- [ADR-0003](adr/0003-owned-startup-vector-linker.ja.md): startup/CRT/vector/linkerは**own実装**。共通crt0+family別vector include(将来device-data生成)、コンストラクタ呼び出し込み、VectorInRAMはld切替
- [ADR-0004](adr/0004-runtime-and-cxx.ja.md): **newlib-nano default、printf %fはmenu opt-in、GNU++17**(-fno-exceptions/-fno-rtti/-fno-threadsafe-statics)。コアAPIはprintf非依存。ltoa/ultoa/dtostrfはcore提供
- [ADR-0005](adr/0005-board-structure-and-fqbn.ja.md): boardは**family単位+pnumメニューに全型番**。boards.txt/ld/variantはdevice-dataから自動生成(手編集CI拒否、locked commit検証)。暫定FQBN=`ch32-riscv-ug:ch32v:<BOARD>:pnum=<型番>`

運用上の決定(ADR外):

- 本プロジェクトは`ch32-riscv-ug`(ユーザーグループ、WCH公式ではない)配下。旧コアのindex/名前空間は捨てる
- Board Manager indexはコアが1つの間は本repoから直接配信。複数化したらlang-ship方式(統合index repo+release完了kick)へ移行(Q-054解決)
- バッチはGitHub Actions(コスト制約なし)。GitHub Pagesは有効(`/`全体公開中: https://ch32-riscv-ug.github.io/ArduinoCore-CH32/ )
- 実機が使えない期間は実機なしで進む作業を優先。必要に応じてrepository分離
- 公開APIはArduino標準/ArduinoCore-API準拠。EVT API・vendor headerを利用者に要求しない。EVT互換は初期release要件にしない
- 書き込み先は複数台から決定的に指定(USB PPPSは不採用)。fixture構成は[upload-and-fixture](upload-and-fixture.ja.md)
- vendorファイルは無断コピーしない([vendor-policy](vendor-policy.ja.md))。文書は日本語`.ja.md`

## 実装済みの資産(コア実装で流用するもの)

- `prototypes/startup/crt0_ch32.S`: 統合startup(13バリアントでEVT等価性検証済み、CI常時実行)
- `prototypes/platform/`: platform.txt/生成boards.txt/own ld/スタブコア(FQBN動作・26 SKU compile・size baseline gate付き)
- `prototypes/generator/generate.py`: device-data tables→boards.txt/ld生成(locked commit方式)
- `prototypes/index/`: xPack直リンクtool定義(6 host、checksum済み)、index生成、clean install検証
- `prototypes/sizebench/`: newlibサイズ計測(toolchain更新時の回帰用)
- コアが提供すべき関数の判明分: `ltoa`/`ultoa`/`dtostrf`、HAL(millis/delay等)、syscalls(_write/_sbrk等)

## 次に始める作業(コア実装フェーズ)

1. **Q-010**: ArduinoCore-APIの固定version(実験0007はcommit `0f4e57e`で無改変compile確認済み)と取込方法(LGPL-2.1配布、symlinkなしrelease)をADR化
2. **Q-013**: 内部HAL contractの範囲を決め、digital/time/Serialから実装(まずcompile+ELF検査、Q-016のhost testを並走)
3. prototypesの構成を実構成(cores/arduino等)へ昇格し、CIを移し替える
4. 残る生成器拡張: variant(pin map)生成はArduinoピン設計合意後。V103/H417のstartup対応は対象family追加時
5. (実機入手後)Q-001の初期SKU確定、probe-rs認定(Q-040系)、HIL一気通貫

## 未決定のまま実装に影響する主要論点

[未決定事項](open-questions.ja.md)参照。特にQ-010(API取込)、Q-013(HAL境界)、Q-016(host test方式)、Q-001(初期SKU、実機待ち)、Q-030(vendor header再配布条件。startupはADR-0003で不要化済み)。

## 新しいスレッドでの開始文

> `/home/mt/dev_wch/ArduinoCore-CH32`でCH32向けArduinoコアを開発しています。まず`docs/handoff.ja.md`を読んでください(決定済み事項はADR-0001〜0005、検証済み資産はprototypes/、実験の根拠はdocs/experiments/)。環境整備フェーズは完了しており、これからコア本体の実装に入ります。入口はQ-010(ArduinoCore-APIの固定versionとLGPL配布方法)とQ-013(内部HAL contract)です。EVT・公式PDF・旧コアは参照のみとし、新repositoryへコピーしないでください(ADR-0003によりstartup/ldはown実装済み)。toolchainはxPack riscv-none-elf-gcc 14.3.0-1(ADR-0002)、検証はprototypes配下のscript群とGitHub Actions(3 OS green)で回ります。
