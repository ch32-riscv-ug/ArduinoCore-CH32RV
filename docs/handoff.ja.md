# 引継ぎメモ

文書基準日: 2026-08-20(2026-08-19にADRの承認状態を見直し)

## 現在地

`ArduinoCore-CH32`は、旧[`arduino_core_ch32_riscv_noneos`](https://github.com/ch32-riscv-ug/arduino_core_ch32_riscv_noneos)を修復せず、長期保守を前提に新規設計するプロジェクトです。対象は全CH32ファミリ(11 family / 27 series / 103型番)。

環境整備は完了し、2026-08-19に**prototypes/を実構成へ昇格**しました。リポジトリのルートがそのままArduino platformディレクトリで、Board Manager経由のclean installとcompileが通る状態です。**コア本体(実API)の実装はこれから**です。

リポジトリには設計文書、[事前調査](research/README.ja.md)、[実験記録0001〜0008](experiments/0001-xpack-multilib-smoke.ja.md)、[ADR-0001〜0009](adr/README.ja.md)(すべて`Proposed`)、3 OSでall greenの[CI](../.github/workflows/ci.yml)があります。

device databaseは独立[`ch32-device-data`](https://github.com/ch32-riscv-ug/ch32-device-data)が正本([ADR-0001](adr/0001-device-data-repository.ja.md)、[境界](device-data.ja.md))。

**実装が入っているが承認されていないものは[承認状態](approval-status.ja.md)に一覧があります。**
コードやCIが緑であることと、採用が決まっていることは別です。

## 提案済み(いずれも未承認)

2026-08-19時点で、maintainerが明示承認した設計判断はありません。以下はすべて事前調査フェーズの**提案**であり、ADR-0001〜0009は`Status: Proposed`です([承認プロセス](adr/README.ja.md))。実測裏付けの有無は「その選択肢が成立する」ことの証明であって、採用の承認ではありません。

大きい順(L0=目的/完成条件 → L1=初期スコープ → L2=実装骨格 → L3=個別技術判断)に確認していきます。

ADR化されている提案:

- [ADR-0001](adr/0001-device-data-repository.ja.md): device databaseは独立repository。Arduino coreは固定versionのconsumer
- [ADR-0002](adr/0002-toolchain-distribution.ja.md): default toolchainは**xPack riscv-none-elf-gccのGitHub Releases直リンク参照**(候補14.3.0-1)。WCH forkは比較lane限定
- [ADR-0003](adr/0003-owned-startup-vector-linker.ja.md): startup/CRT/vector/linkerは**own実装**。共通crt0+family別vector include(将来device-data生成)、コンストラクタ呼び出し込み、VectorInRAMはld切替
- [ADR-0004](adr/0004-runtime-and-cxx.ja.md): **newlib-nano default、printf %fはmenu opt-in、GNU++17**(-fno-exceptions/-fno-rtti/-fno-threadsafe-statics)。コアAPIはprintf非依存。ltoa/ultoa/dtostrfはcore提供
- [ADR-0005](adr/0005-board-structure-and-fqbn.ja.md): boardは**family単位+pnumメニューに全型番**。boards.txt/ld/variantはdevice-dataから自動生成(手編集CI拒否、locked commit検証)。暫定FQBN=`ch32-riscv-ug:ch32v:<BOARD>:pnum=<型番>`
- [ADR-0006](adr/0006-rtos-policy.ja.md): **コアはベアメタル単一セマンティクス**。初期リリースはRTOSなし、将来はコア同梱FreeRTOSライブラリ(UNO R4方式)が第一候補。tickソース差替可能なHALとConfig置換フックをQ-013で先行実装
- [ADR-0007](adr/0007-user-build-option-injection.ja.md): **`build.extra_flags`はコアで使わずユーザー注入専用**(--build-property/boards.local.txt)。CIが注入到達を常時ガード
- [ADR-0008](adr/0008-upload-strategy.ja.md): **書き込みdefaultはWCH-LinkE**(probe-rs系frontend、gap familyはwlink/OpenOCD併用)。開発もLinkEで進め、USB-ISP/UART-ISP/board固有BL(UIAPduino等)を段階追加。互換programmerはTier管理

運用上の前提(ADR外、いずれも未承認):

- 本プロジェクトは`ch32-riscv-ug`(ユーザーグループ、WCH公式ではない)配下。旧コアのindex/名前空間は捨てる
- Board Manager indexはコアが1つの間は本repoから直接配信。複数化したらlang-ship方式(統合index repo+release完了kick)へ移行(Q-054解決)
- バッチはGitHub Actions(コスト制約なし)。GitHub Pagesは有効(`/`全体公開中: https://ch32-riscv-ug.github.io/ArduinoCore-CH32/ )
- 実機が使えない期間は実機なしで進む作業を優先。必要に応じてrepository分離
- 公開APIはArduino標準/ArduinoCore-API準拠。EVT API・vendor headerを利用者に要求しない。EVT互換は初期release要件にしない
- 書き込み先は複数台から決定的に指定(USB PPPSは不採用)。fixture構成は[upload-and-fixture](upload-and-fixture.ja.md)
- vendorファイルは無断コピーしない([vendor-policy](vendor-policy.ja.md))。文書は日本語`.ja.md`

## 2026-08-19に確認したこと(maintainer承認済み)

このセッションで明示的に合意した項目です。ADR化はこれから。

- ADR-0001〜0008は自己承認だったため全て`Proposed`へ戻し、**大きい順(L0→L1→L2→L3)に確認していく**
- **v1.0の完成条件は「実用最小」**: GPIO/UART/SPI/I2C/ADC/PWM/割込みが1〜2 familyでTier A。実際のArduino sketchが書ける水準
- **実機は手元にある**(代表的な機材は所有済み)。残るのは配線で、**どのboardをどう配線するかは未確認**
- **ArduinoCore-APIは使う。repoに実体をコミットする**(→[ADR-0009](adr/0009-arduinocore-api-import.ja.md))
- **ArduinoCore-APIの更新は積極的に行わない**(変更頻度が低く、追随自体に価値がないため)
- コア拡張(`Serial.printf()`等)の置き場所は**未承認**([Q-019](open-questions.ja.md))
- **主対象はCH32X035**。X035固有機能もリリース対象外にはせず、**できるところまで載せて出す**。**USB-PDまわりは優先度高め**
- **旧実装(`arduino_core_ch32_riscv_arduino`)は参考程度**。ゼロベースでより良い設計を作る。[監査結果](legacy-audit.ja.md)は「どこにfamily差が出るか」の観測データとして使い、構造をそのまま踏襲しない
- WCH-LinkEの複数台識別は**当面1台のみで進めながら検討**。[`board-identify`](https://github.com/tanakamasayuki/board-identify)方式(ターゲット自身から識別子を読む)が有力
- **最初のマイルストーンは「主要boardで`Serial.println()`が通る」**。周辺機能とfixture配線(LA接続)はその後
- **自動testは`pytest-embedded-arduino-cli`単体**で組む(他プロジェクトと同一構成。ArduTestは使わない)。**board切り替えは`sketch.yaml`のprofile**

## 現在の資産

すべてCIで常時検証されています(4 job、ローカルでも全green確認済み)。

| path | 内容 | CI job |
|---|---|---|
| `cores/arduino/api/` | ArduinoCore-API 1.5.2 無改変snapshot(47ファイル、LGPL-2.1-or-later) | `api-sync`(upstreamとbyte一致) |
| `cores/arduino/crt0_ch32.S` | 統合startup。EVT等価性を13バリアントで検証(39 check OK)。**CH32V003実機で動作確認済み**(実験0010) | `startup-equivalence` |
| `cores/arduino/{Arduino.h,main.cpp}` | ArduinoCore-APIへの接続とentry point | `compile-matrix` |
| `cores/arduino/{ch32_registers.h,ch32_gpio.h}` | 自前の最小レジスタmapとGPIO primitive(family差を吸収) | `compile-matrix` |
| `cores/arduino/{wiring_digital.c,wiring_time.c}` | GPIO / clock / SysTick / millis / delay。**V003実機で動作確認済み** | `compile-matrix` |
| `cores/arduino/HardwareSerial.{h,cpp}` | 割込み駆動UART。**V003実機で送受信確認済み** | `compile-matrix` |
| `cores/arduino/{syscalls.c,itoa.c,dtostrf.c}` | newlib syscallと`ltoa`/`ultoa`/`dtostrf` | `compile-matrix` |
| `tests/manual/` | 実機bring-up runner。`smoke.py`(出荷経路でのcompile→upload→UART確認)と`uart_scan.py`(boardがどのUSARTを配線しているか特定) | 手動(要実機) |
| `tools/index/tools_probe_rs.json` / `probe_rs_targets.csv` | probe-rs 0.32.0のtool定義とchip名map | `install-test`(3 OS、`.tar.xz`展開と実行確認) |
| `programmers.txt` | `wch-link` programmer(probe-rs経由のSWD書き込み) | `install-test` |
| `boards.txt` / `variants/<SERIES>/` | device-dataからの生成物(23 series board / 117エントリ、ld + pin map)。locked commit | `generated-sync` |
| `platform.txt` | ビルドrecipe。`build.extra_flags`はユーザー注入専用 | `compile-matrix`(注入到達ガード) |
| `tools/generate/generate.py` | boards.txt / ld / pin map / vector include の生成 | `generated-sync` |
| `tools/index/` | xPack直リンクtool定義、index生成、clean install検証 | `install-test`(3 OS) |
| `tools/vendor/check_api_sync.py` | api/のbyte一致とlock manifest検証 | `api-sync` |
| `tests/compile/` | 117エントリ compile matrix + size baseline(完全一致gate) | `compile-matrix`(3 OS) |
| `tests/startup/` | EVT startupとの等価性harness | `startup-equivalence` |
| `tests/sizebench/` | newlibサイズ計測(toolchain更新時の回帰用) | 手動 |
| `vendor/arduino-core-api.lock.toml` | 第三者取込のlock(commit + 全ファイルSHA-256) | `api-sync` |

新規に判明: **arduino-cliは`cores/arduino/api/*.cpp`(8本)を実platformで自動compileし、`--gc-sections`が未使用分を完全に落とす**。api/追加後もBlinkのサイズは26 SKU全てでbaselineとバイト一致(476/4/520)。

コアが提供すべき関数(2026-08-19に実物で確認し、実験0007の記述を細分化):

| 関数 | 状況 |
|---|---|
| `itoa` / `utoa` | newlibが持つ。対応不要 |
| `ltoa` / `ultoa` | **coreが実装する**。upstreamは`api/itoa.h`で宣言のみ |
| `dtostrf` | **upstreamが`api/deprecated-avr-comp/avr/dtostrf.c.impl`に実装を持つ**。coreは自前の`.c`からincludeするだけでよい(samd/renesasと同じ) |

その他: HAL(millis/delay等)、syscalls(`_write`/`_sbrk`等)。

## 次に始める作業

### Milestone 1: 主要boardで`Serial.println()`が通る

**CH32V003で達成**(2026-08-20、[実験0011](experiments/0011-milestone1-serial-on-v003.ja.md))。
受け入れtestの3項目が実機で通り、送受信・SysTick精度・`F_CPU`一致まで確認済み。

```
hello from ch32
int=42
hex=BEEF
```

**V003 / V203 / X035 / L103 の4 familyで実機確認済み**。runnerは繋いで1コマンド:

```bash
uv run tests/manual/chip_info/chip_info.py                 # 何が繋がっているか
uv run tests/manual/smoke/smoke.py --board CH32X035    # 受け入れsketch
```

boardごとのSerial結線(TX/RX)は[tests/manual/README.ja.md](../tests/manual/README.ja.md)の表。

1. **V003 / V203 / L103をroute変更後のpinで回し直す**。UART routeをreset既定優先へ
   変えた(手元のboardもWCH公式コアも旧コミュニティコアも既定pinへ配線しているため)ので、
   これらの確認は変更前のpinで取ったものになっている。X035は変更後で確認済み
2. **V103 / V307 の実機確認**。V103は**vector tableがジャンプ表の唯一のfamily**
3. **`gpio_loopback`を実機で回す**。ジャンパ1本でGPIOのレベル / pull / 別ポートEXTI /
   PWM dutyが見える。compileのみ確認済み
4. ~~書き込み経路の実体化~~ → **完了**([実験0012](experiments/0012-probe-rs-upload-toolchain.ja.md))

### 2026-08-20に見つけて直したこと

**libglossのsemihosting stubがheapとprintfを黙って壊していた**
([実験0014](experiments/0014-libgloss-semihosting-stubs.ja.md))。
`String`を使うsketchはリセットループになり出力が一切出ず、`printf`は無音で成功していた。
どちらもcompile testでは絶対に見つからない。`--start-group`で`core.a`を再走査させ、
`_sbrk`を別objectへ分離し、`HardwareSerial.h`が`pins_arduino.h`を自分でincludeするようにした。

`--specs=nano.specs`を既定にする案も入れた(printfを使うsketchが48 KB → 7.1 KBになり、
CH32V003(16 KB)にも載る。`%f`は`menu.printf`でopt-in)。ADR-0004が同じ形を提案しているが
**同ADRは`Proposed`で、これは承認されていない**([承認状態 A-1](approval-status.ja.md))。

**テスト計画**([tests/TEST_PLAN.ja.md](../tests/TEST_PLAN.ja.md))を作成し、
`tests/hardware/` を `tests/manual/` へ集約、`chip_info.py`を追加。
Board Manager配布経路はupgrade/rollbackまで検証でき、release workflowも用意した
(**未実行**)。probe-rsのWindowsアーカイブ再ホストは**未承認**
([承認状態 A-2](approval-status.ja.md))。

pin mapは生成済み([ADR-0010](adr/0010-pin-numbering.ja.md))。`variants/<SERIES>/pins_arduino.h`が
pad名・ポート別validity mask・`A<n>`(ADC1)・USART pinとAFIO remapを持ち、
`cores/arduino/ch32_pins.h`が番号encodingを持つ。

### 並行して決める

- **Q-001**: 対象boardの確定(X035が主。C8T6以上でないとI2C/USB/SWDが両立しない)
- **Q-013**: 内部HAL contract。[旧コア監査](legacy-audit.ja.md)は境界の観測データとして使う
- **Q-019**: コア拡張(`Serial.printf()`等)の置き場所
- ADR-0001〜0009を大きい順に確認して`Accepted`にしていく

### あとで

- fixture配線(LA channel数とconnector、Q-050)。**16ch推奨**の根拠は[upload-and-fixture](upload-and-fixture.ja.md)
- `CH32_UNUSABLE_PINS`をcore側で実際に弾く(現在はvariantが宣言するだけ)

## 先送りにした作業

[TODO](todo.ja.md)に積み上げる。実装を簡略化するたびに1行足すこと。

## 未決定のまま実装に影響する主要論点

[未決定事項](open-questions.ja.md)参照。特にQ-010(API取込)、Q-013(HAL境界)、Q-016(host test方式)、Q-001(初期SKU、実機待ち)、Q-030(vendor header再配布条件。startupはADR-0003で不要化済み)。

## 新しいスレッドでの開始文

> `/home/mt/dev_wch/ArduinoCore-CH32`でCH32向けArduinoコアを開発しています。まず`docs/handoff.ja.md`を読んでください。**ADR-0001〜0009はすべて`Proposed`(maintainer未承認)**で、大きい順に確認していく方針です。prototypes/は実構成へ昇格済みで、リポジトリのルートがそのままArduino platformディレクトリです(`cores/arduino/api/`にArduinoCore-API 1.5.2の無改変snapshot)。CIは4 job・3 OSでgreen。v1.0の完成条件は「代表familyでGPIO/UART/SPI/I2C/ADC/PWM/割込みがTier A」、実機は所有済みで配線が未確定です。次はQ-013(内部HAL contract)とQ-001/Q-050(対象board確定と配線)。EVT・公式PDF・旧コアは参照のみとし、新repositoryへコピーしないでください。toolchainはxPack riscv-none-elf-gcc 14.3.0-1です。
