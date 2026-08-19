# R-16: RTOSサポート方針の事前調査

調査基準日: 2026-08-19
関連: Q-006(→[ADR-0006](../adr/0006-rtos-policy.ja.md)で決定済み)、[Q-003](../open-questions.ja.md)、boardメニュー構成([ADR-0005](../adr/0005-board-structure-and-fqbn.ja.md))

## 調査目的

EVTには複数のRTOS移植例が含まれる。Arduinoコアとして「RTOS上に構築する/オプション提供する/各自ライブラリ導入に任せる」のどれを取るかを、EVTの実態と主要Arduinoコアの前例から判断できる材料にする。boardマネージャのメニュー構成への影響を含む。

## 確認済み事実(ローカル: EVTの実態)

### RTOS例のfamily別の有無

| family | RTOS例 |
|---|---|
| **CH32V003 / CH32V00X** | **なし**(WCH自身が小容量には提供していない) |
| V103, V20x, V205, V307, X035, L103, M030 | FreeRTOS / RT-Thread / TencentOS / HarmonyOS(LiteOS-m)の4種 |
| X315 | `CPU/OS/`配下に同4種 |
| V407, H417 | RTOS例なし(CPU配下はCoreMark/INT等のみ) |

### WCHのFreeRTOS移植の中身(V307で確認)

- カーネルは**FreeRTOS公式V10.4.6**+公式GCC/RISC-Vポート(`portable/GCC/RISC-V`)+WCH固有拡張ヘッダ(`chip_specific_extensions/RV32I_PFIC_no_extensions`)
- **SysTick(WCH STK)をtickに、Software IRQをcontext switchに占有**する。port.cはmtvecの下位2bitが0b11(絶対アドレス+vectored)であることを確認する(統合crt0の初期化と整合)
- **FreeRTOS用startupはCSR初期値が異なる**: INTSYSCR(0x804)=0x0b→**0x1f**、mstatus=0x6088→**0x7800**(=MIE/MPIE無効で起動し、scheduler開始まで割込みを止める)。差分はこの2定数のみで、[ADR-0003](../adr/0003-owned-startup-vector-linker.ja.md)のcrt0 `-D`注入軸で表現できる
- EVTのFreeRTOSConfigはheap 12KB、最小stack 256等 → **V307級(RAM 64K)は余裕、V00X(8K)は限界的、V003(2K)は実用不能**

## 確認済み事実(前例: 主要Arduinoコア、2026-08-19確認)

| コア | 方式 | 仕組みの要点 |
|---|---|---|
| arduino-esp32 | **常時RTOS**(FreeRTOS/IDF) | setup/loopがタスク。IDFはプリコンパイル配布のため**FreeRTOSConfig相当を利用者が変更できない**(公式FAQ)。RAM 320KB+前提 |
| arduino-pico | **メニュー+同梱ライブラリ** | `#include <FreeRTOS.h>`+Tools→OSメニューで`-D__FREERTOS`注入、カーネルはソースビルド。**FreeRTOSConfigがコア固定で上書き不可という未解決の弱点**。CIはOS有無で全数二重化していない |
| STM32duino | **独立ライブラリ**(STM32FreeRTOS) | コアはベアメタル。configはスケッチ側`STM32FreeRTOSConfig(_extra).h`で置換/追記。コア側に`build_opt.h`(GCC @file)という汎用フラグ注入フックがあり「ライブラリは-Dを注入できない」というArduino仕様の制約を回避 |
| ArduinoCore-mbed | 常時RTOS(Mbed OS) | **Mbed EOL(2026-07)によりコア全体をZephyrへ書き直し中**。外部OS常時依存のリスクの実例 |
| ArduinoCore-renesas(UNO R4) | **コア同梱ライブラリ、メニューなし** | `libraries/Arduino_FreeRTOS`同梱。include一発で有効、未使用時コストゼロ |
| openwch公式CH32 core | なし | ベアメタル |

その他の裏付け:

- FreeRTOS公式カーネルは**V10.5.0でGCC RISC-VポートにRV32E対応を追加済み**(ISA面ではV003級もカバー)。ただしQingKeのPFIC/独自CSRは非標準で、mainline移植では`ecall`プリエンプトとINTSYSCR設定の調整が必要だった事例報告あり
- **CH32V003(RAM 2KB)での実用FreeRTOS事例は確認できず**。V003界隈のデファクトはRTOSなし(ch32fun)
- 「RTOS有無」をboards.txtメニューにした前例はarduino-picoの`menu.os`が唯一。boards.txtはboard(=family)ごとにメニュー定義できるため、**「V00X系にはRTOSメニューを出さない」という区切りが可能**

## 推奨(提案)

ユーザーの3択(採用/不要/各自導入)への回答:

1. **コアをRTOS上に構築する案(ESP32型)は不採用**。RAM 2KB(V003)〜の下端で物理的に成立せず、コアのセマンティクスが2系統になる。mbed EOLの教訓(外部OS常時依存はコア全体の書き直しリスク)もある。**大容量family(V307/V407/H417)でも同様**とし、コアは全family単一のベアメタルセマンティクスを保つ
2. **初期リリースはRTOSなし**。現行openwch公式もRTOSなしで、需要の実証もまだない
3. 需要が出た段階で「**コア同梱FreeRTOSライブラリ(UNO R4方式: include一発、メニューなし)**」を第一候補として追加する。コアAPI統合(delay/yieldのscheduler協調)が必要になった場合だけ、arduino-pico方式のメニュー(`-D`注入)を**RTOS対象familyに限定して**追加する二段構え
4. RTOSカーネルは**EVTの改変版を固定化せず、FreeRTOS公式(≥V10.5.0)ベース**にし、PFIC/CSR差分をport層に局所化する(EVT移植は挙動の参照資料)

### 実装フェーズへ今から織り込むべきフック(コスト小)

- crt0のCSR注入軸は既にRTOS用初期値(INTSYSCR/mstatus)を表現できる(追加作業なし。ADR-0003の設計が有効と確認)
- weakハンドラ設計により、ライブラリがSysTick/SW handlerを強シンボルで占有できる(追加作業なし)
- **millis/delayのtickソースを差し替え可能なHAL設計にする**(Q-013の要件に追加。RTOS時はtick hookからmillisを供給)
- **`build_opt.h`相当のフック+スケッチ側Config置換の仕組みをコアに最初から実装する**(STM32duinoで実証済み。RTOS以外にも有用)
- コア同梱ライブラリはプリコンパイル(.a)配布にしない(config変更を殺すため)

### メニュー構成への影響

- **初期リリース: メニュー軸なし**(FQBN・CI matrixへの影響ゼロ)
- 将来メニューを入れる場合も、family単位board([ADR-0005](../adr/0005-board-structure-and-fqbn.ja.md))なのでV00X/V003系boardにはメニュー自体を出さない。CIは代表boardのみOS別ビルド(arduino-pico同様、全数二重化はしない)

## 判断ポイント

- RTOS提供開始のトリガー(利用者需要の確認方法)
- 同梱ライブラリ vs 独立repositoryライブラリ(release独立性 vs 導入の手軽さ。UNO R4方式=同梱が有力)
- FreeRTOS以外(RT-Thread等)の要望が来た場合の受け口(独立ライブラリとしてコミュニティに開放する形が現実的)
- H417 dual core(V5F+V3F)でのRTOS/AMP構成は完全に別論点として保留

## 未検証事項

- WCH port(chip_specific_extensions)とFreeRTOS公式最新カーネル(V11系)の組合せ動作
- V00X(RAM 4〜8K)での最小FreeRTOS構成の実測(技術的可否の確認。実用性は別)
- RTOS動作時の割込みlatency/HPEとの相互作用(Q-021と併せて実機で)
