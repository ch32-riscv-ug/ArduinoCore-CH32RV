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
| R-09 | newlib/ilp32e runtimeのサイズ実測 | RV32EmC/RV32ECでのprintf、constructor、C++コスト | Q-022, Q-051 | **計測済み**([実験0006](../experiments/0006-newlib-size-baseline.ja.md): nano必須、full printf=47KB/new=114KB、%f opt-in=+19.5KB、C++機能自体は軽量) |
| R-10 | uploader/書き込みtool | probe-rs等のbackend認定 | Q-040〜Q-048 | frontend/fixture設計は[既存文書](../upload-and-fixture.ja.md)、エコシステム調査はR-17 |
| R-11 | vendorファイルのライセンス・再配布 | EVT由来ファイルの取込条件 | Q-030, Q-034 | [既存文書](../vendor-policy.ja.md)で管理 |
| R-12 | device databaseとboards.txt生成の連携 | exact SKU正本からの生成 | Q-011, Q-014 | [ADR-0001](../adr/0001-device-data-repository.ja.md)と[device-data](../device-data.ja.md)で管理。R-03に生成方針の提案あり |
| R-13 | USB(CDC/HID)対応family | X035/V20x/V307等のUSB stack方針 | Q-003 | 未着手(初期scope外の見込み) |
| R-14 | ArduinoCore-APIの取込とhost test | 固定version、LGPL配布 | Q-010, Q-016 | [既存文書](../architecture.ja.md)で管理 |
| R-15 | [開発中コアのインストール方式とテスト環境](local-install-and-test-env.ja.md) | symlink直接実行と、ローカルHTTP+arduino-cli経由の実インストール検証の使い分け | Q-015, Q-016 | 方式A/Bとも検証済み |
| R-16 | [RTOSサポート方針](rtos-support.ja.md) | EVTのRTOS移植の実態と他コア前例から、コア層での採用/オプション/ライブラリ任せを判断 | Q-006, Q-003, Q-013 | 調査済み・[ADR-0006](../adr/0006-rtos-policy.ja.md)で決定 |
| R-18 | [probe-rsのアーカイブ構造とarduino-cliの要求](probe-rs-archive-layout.ja.md) | Windowsだけ Board Manager install が失敗する原因の特定と方針比較 | Q-054 | 調査済み・[ADR-0011](../adr/0011-tool-mirror-repository.ja.md)で方針決定 |
| R-19 | [device-dataのsignal名正規化](signal-name-normalization.ja.md) | リマップAPIの`setPins(tx, rx)`が要求するpin名→ルート値の逆引き表を、device-dataから生成できるようにするための不足分の特定 | Q-011, Q-014 | **調査済み・上流へ依頼作成。作業深さは判断待ち** |
| R-20 | [レジスタマップに必要なデータの列挙](register-map-data.ja.md) | ESP32/STM32duino相当のレジスタマップを持つとしたら、どのデータをどの粒度でdevice-dataへ依頼するか(D-1〜D-8)。既存のch32-rs/ch32-dataとの関係も | Q-011 | **列挙のみ。同梱の可否・形・上流の選択はいずれも判断待ち** |
| R-21 | [route定数の書式候補](pin-name-format.ja.md) | `setPins()`へ渡すpadをエディタ補完で選べるようにする定数の書式。define / namespace+constexpr / route単位 / enum class を実データで比較 | Q-011 | **候補のみ。採用は未決** |
| R-22 | [USB device/hostのスタック選定](usb-stack.ja.md) | TinyUSBかベンダ独自か。TinyUSBのCH32対応範囲、WCH側がhost FS層をバイナリでしか出していないこと、X035はPR未マージであること、そしてX035以外はPLLが先という順序 | Q-011 | **調査済み・採用は判断待ち** |
| R-23 | [TinyUSBのベンダヘッダ依存をどう外すか](tinyusb-vendor-header.ja.md) | TinyUSBのCH32 driverが要求する4つの記号の特定、上流refactor / opt-in追加 / 同名shim の比較、既存ユーザーへの影響 | Q-011 | **決定(2026-08-21): まずshim(B)。上流提案(A')はR-20が形になってから** |
| R-24 | [クロック関連データの整備依頼](clock-data-request.ja.md) | PLL対応に要る事実(ツリーの段数・PLL field・非RCCレジスタ・APB上限・flash latency・USB 48MHz経路)をC-1〜C-8として上流へ依頼する案。検証方法込み | Q-011 | **依頼案。上流へ未提出** |
| R-25 | [coreに置くものと、ライブラリに出すもの](core-scope.ja.md) | coreの範囲を決める3つの基準、`SerialSDI`のライブラリ化、printfの出力先差し替え、examplesの置き場所と規約、レジスタヘッダを公開することの懸念 | Q-011 | **一部実施済み(SerialSDI移動・stdout・examples)。基準のADR化は判断待ち** |
| R-26 | [EEPROMをどう用意するか](eeprom.ja.md) | CH32にEEPROMは無くflash emulationになる。X035にword書き込みが無いため共通単位はfast page(64/128/256B)。置き場所・書き込みモデル・摩耗の3軸で選択肢 | Q-011 | **選択肢の提示。判断待ち** |
| R-17 | [書き込み経路・書き込み器・書き込みソフト](upload-programmers.ja.md) | family×書き込み経路の対応表、互換programmerエコシステム、default/オプションのメニュー構成案 | Q-040〜Q-049 | 調査済み・[ADR-0008](../adr/0008-upload-strategy.ja.md)で方針決定(実機認定は未) |

## 主要な結論(要約)

- **R-01**: startupの統合は可能。family差は「vector table」「CSR初期化定数」「FPU/VS」「highcode」「H417 V5F特殊boot」の5軸に整理でき、reset処理はプリプロセッサで完全にパラメータ化できる。本丸はvector tableの記述方法(手書き統合 vs device-data生成)。「共通crt+vector include」方式は3 familyのELF等価性検証で成立を確認済み([実験0002](../experiments/0002-unified-startup-poc.ja.md))
- **R-02**: menu軸候補はSKU、clockプロファイル、HSE値、FLASH/RAM分割、VectorInRAM(ld差分のみ)、debug出力先。vendorのsystem_*.cはSYSCLK選択がハードコードで-D上書き不可のため、clock初期化はown実装が有力
- **R-03**: 「26バリエーション」はV00X系6 seriesの英語版datasheet合計と一致(zh版のみのCH32M006で27)。推奨構成は**family単位11ボード+pnumメニューに全型番**(STM32duino/openwch型)、boards.txt/variantはdevice-dataから完全自動生成
- **R-17**: defaultは**WCH-LinkE(probe-rs系frontend)**、オプションはUSB-ISP(wchisp。X03x/X315/H417は1200bps touch+SWエントリで自動書き込み化可)/UART-ISP(V00X系)/board固有BL(UIAPduino=rv003usb等)。**probe-rsのcoverage gap(V407/X315/M030/V205)**と「工場BLへのSWエントリ可否でfamilyが2系統に割れる」構造が判明
- **R-16**: **コアはRTOS上に構築しない(全familyベアメタル単一セマンティクス)**。初期リリースはRTOSなし、需要が出たら「コア同梱FreeRTOSライブラリ(UNO R4方式)」→必要時のみ限定familyメニューの二段構え。V003/V00XにはWCH自身もRTOS例を提供しておらず、RTOS用startup差分(CSR 2定数)はADR-0003のcrt0で表現可能
- **R-04**: **ESP32のtoolchainはrv32e/ilp32e欠落でCH32V003/V00Xに使用不可**。推奨は**xPack riscv-none-elf-gcc(14.3.0-1基準)のGitHub Releases直リンク参照**(STM32duinoに同方式の実績)。WCH fork(MRS GCC12)は比較lane限定。multilib実物と全core向けcompile smokeは[実験0001](../experiments/0001-xpack-multilib-smoke.ja.md)で確認済み

## 文書間の関係

- R-01/R-02は「startup/vector/linkerを手書き・template生成・外部data生成のどれにするか」(Q-012)の入力です
- R-03は「最初に正式対象とするSKU」(Q-001)と「FQBN/menu構造」(Q-015/Q-017)の入力です
- R-04は「default GCC distribution/version」(Q-020)と「取得・再配布条件」(Q-026)の入力です
- 選定原則そのものは[toolchain方針](../toolchain.ja.md)が正本であり、R-04は候補の実態調査です
- R-15は[テスト戦略](../test-strategy.ja.md)のCI設計とrelease前確認手順の入力です
