# 事前調査

調査基準日: 2026-08-19

このディレクトリは、コア実装開始前の事前調査を「調査目的 → 調査結果 → 推奨 → 判断ポイント」の形で残すためのものです。[未決定事項](../open-questions.ja.md)のQ-IDと対応付け、決定はADRへ進めます。

ここに書くのは調査結果と推奨であり、決定ではありません。

## 調査リストの作り方

依頼された4テーマ(startup、EVT構造とboardオプション、SKUバリエーション、toolchain)から出発し、「Arduino coreとして1つのsketchがbuild・書き込み・実行されるまでに必要な決定」を広く洗い出してリスト化しました。対象は特定familyではなく**全CH32ファミリ**(11 family / 27 series / 103型番)です。既存文書で扱っている項目は重複調査せず、リンクだけ置きます。

## 調査リスト

| ID | テーマ | 目的 | 関連Q | 状態 |
|---|---|---|---|---|
| R-01 | [startupファイル(.S)の横断調査](startup-files.ja.md) | family別バリエーションの実態把握と、単一ファイル+プリプロセッサ統合の可否判断 | Q-012, Q-031 | **調査済み** |
| R-02 | [EVT構造とboardオプション軸](evt-structure.ja.md) | EVTの構成(linker、clock初期化、デバイス選択マクロ、debug出力)からboard menuの軸を抽出 | Q-001, Q-012, Q-013, Q-015 | **調査済み** |
| R-03 | [SKUバリエーションとboard/menu構造](board-variants-and-menus.ja.md) | 全103型番(V00X系26を含む)の実体確認と、board選択・menu構造の設計案。他コア前例含む | Q-001, Q-015, Q-017 | **調査済み** |
| R-04 | [toolchain配布物](toolchain-distributions.ja.md) | GCCを自前ビルド・同梱せず、公開配布物をBoard Managerから利用できるかの確認と候補選定 | Q-020, Q-022, Q-025, Q-026 | **調査済み** |
| R-05 | 他Arduino coreのboard/menu構造の前例 | 26 SKU超をIDEでどう見せるかの前例収集 | Q-015, Q-017 | R-03に統合 |
| R-06 | linker scriptとメモリ構成 | SKU別FLASH/RAM、RAM分割(V20x/V307/V407)、vector配置の扱い | Q-012 | R-02に統合 |
| R-07 | clock初期化とクロックメニュー | system_ch32*.cのSYSCLK選択肢とHSE有無 | Q-013 | R-02に統合 |
| R-08 | 割込みABIとWCH高速割込み(HPE) | interrupt attribute、hardware stack、latencyの実測 | Q-021 | 未着手(実測が必要) |
| R-09 | newlib/ilp32e runtimeのサイズ実測 | RV32EmC/RV32ECでのprintf、constructor、C++コスト | Q-022, Q-051 | 未着手(toolchain認定と併せて実測) |
| R-10 | uploader/書き込みtool | probe-rs等のbackend認定 | Q-040〜Q-048 | [既存文書](../upload-and-fixture.ja.md)で管理 |
| R-11 | vendorファイルのライセンス・再配布 | EVT由来ファイルの取込条件 | Q-030, Q-034 | [既存文書](../vendor-policy.ja.md)で管理 |
| R-12 | device databaseとboards.txt生成の連携 | exact SKU正本からの生成 | Q-011, Q-014 | [ADR-0001](../adr/0001-device-data-repository.ja.md)と[device-data](../device-data.ja.md)で管理。R-03に生成方針の提案あり |
| R-13 | USB(CDC/HID)対応family | X035/V20x/V307等のUSB stack方針 | Q-003 | 未着手(初期scope外の見込み) |
| R-14 | ArduinoCore-APIの取込とhost test | 固定version、LGPL配布 | Q-010, Q-016 | [既存文書](../architecture.ja.md)で管理 |
| R-15 | [開発中コアのインストール方式とテスト環境](local-install-and-test-env.ja.md) | symlink直接実行と、ローカルHTTP+arduino-cli経由の実インストール検証の使い分け | Q-015, Q-016 | 方針記録済み(実測待ち) |

## 主要な結論(要約)

- **R-01**: startupの統合は可能。family差は「vector table」「CSR初期化定数」「FPU/VS」「highcode」「H417 V5F特殊boot」の5軸に整理でき、reset処理はプリプロセッサで完全にパラメータ化できる。本丸はvector tableの記述方法(手書き統合 vs device-data生成)
- **R-02**: menu軸候補はSKU、clockプロファイル、HSE値、FLASH/RAM分割、VectorInRAM(ld差分のみ)、debug出力先。vendorのsystem_*.cはSYSCLK選択がハードコードで-D上書き不可のため、clock初期化はown実装が有力
- **R-03**: 「26バリエーション」はV00X系6 seriesの英語版datasheet合計と一致(zh版のみのCH32M006で27)。推奨構成は**family単位11ボード+pnumメニューに全型番**(STM32duino/openwch型)、boards.txt/variantはdevice-dataから完全自動生成
- **R-04**: **ESP32のtoolchainはrv32e/ilp32e欠落でCH32V003/V00Xに使用不可**。推奨は**xPack riscv-none-elf-gcc(14.3.0-1基準)のGitHub Releases直リンク参照**(STM32duinoに同方式の実績)。WCH fork(MRS GCC12)は比較lane限定

## 文書間の関係

- R-01/R-02は「startup/vector/linkerを手書き・template生成・外部data生成のどれにするか」(Q-012)の入力です
- R-03は「最初に正式対象とするSKU」(Q-001)と「FQBN/menu構造」(Q-015/Q-017)の入力です
- R-04は「default GCC distribution/version」(Q-020)と「取得・再配布条件」(Q-026)の入力です
- 選定原則そのものは[toolchain方針](../toolchain.ja.md)が正本であり、R-04は候補の実態調査です
- R-15は[テスト戦略](../test-strategy.ja.md)のCI設計とrelease前確認手順の入力です
