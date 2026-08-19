# 未決定事項

文書基準日: 2026-08-17

優先度は`P0`が実装開始前、`P1`が最初のvertical slice中、`P2`が初期release前です。

## Scopeと互換性

| ID | 優先度 | 論点 | 判断に必要なもの |
|---|---:|---|---|
| Q-001 | P0 | 最初に正式対象とするexact SKU/package/board | 所有実機、一次資料、fixture配線、利用者需要 |
| Q-002 | P0 | 初期releaseをRISC-V CH32だけに限定するか | CH32F対応の需要とrepository/package構造への影響 |
| Q-003 | P1 | Arduino APIの初期対応範囲とSKU別の未対応表現 | ArduinoCore-API inventory、標準API contract、サイズbudget、capability、利用例 |
| Q-004 | P1 | 旧コアとのソース互換範囲 | 既存sketch/exampleのcompile survey |
| Q-005 | P2 | 初期release後にEVT Compatibility Packを提供する必要があるか | 実用Arduino example提供後の利用者需要、license、維持コスト |

## Architectureとmanifest

| ID | 優先度 | 論点 | 判断に必要なもの |
|---|---:|---|---|
| Q-010 | P0 | `ArduinoCore-API`の固定versionと取込方法 | サイズ試験、LGPL配布方法、symlinkを使わないrelease方法。実験0007で対象commitの無改変compile・サイズ実測済み。残りはversion固定方法とLGPL配布 |
| Q-011 | P0 | device/board manifestのformatとschema実装 | 8 sample prototype、canonical signal、silicon/package正規化、internal route、verification粒度のreview |
| Q-013 | P1 | 内部HAL contractをどこまで設けるか | digital/time/Serial/SPI/I2Cの2 family実装比較 |
| Q-014 | P1 | `ch32-device-data`のrelease/commitをArduinoへ固定する形式 | offline build prototype、hash検証、生成差分、更新手順 |
| Q-016 | P0 | host contract testを何で実行するか | host-arduino-core固定利用、内部HAL mock、native unitの比較 |
| Q-017 | P2 | 公開用FQBN、packager ID、architecture ID | Arduino package互換性、既存公式coreとの衝突確認、暫定IDからの移行 |
| Q-019 | P1 | コア拡張API(`Serial.printf()`等)をどこに置くか | 下記の選択肢比較。`api/`改変の可否がADR-0009のbyte一致検証の形を変える |

### Q-019の選択肢(未決定)

`ArduinoCore-API`の`Print`には**`printf`が存在しない**(1.5.2で確認)。`Serial.printf()`を提供する場合の置き場所は未決定。ESP32/arduino-picoは`Print::printf`を持つが、両者ともArduinoCore-APIを使っておらず自前`Print`のため前例にならない。

| 案 | 内容 | 影響 |
|---|---|---|
| a | `api/Print.h`をpatchして`Print::printf`を追加 | 任意の`Print&`から呼べる。upstream差分が恒久化し、ADR-0009のbyte一致検証をpatch適用後treeとの比較へ組み替える必要がある |
| b | CH32側の派生クラス(`HardwareSerial`等)に追加 | `api/`無改変を維持できる。`Print&`越しの多態呼び出しはできない |
| c | 提供しない(標準APIのみ) | 最小。利用者は`snprintf`+`Serial.print`を書く |
| d | free function / mixin等の別形 | 要調査 |

いずれの案でもサイズ影響は共通: newlib-nanoの`printf %d`が約4.9KB、`%f`のopt-inが+19.5KB(実験0006)。CH32V003(Flash 16K)では`%f`は成立しない。

**前コアの実例(2026-08-19判明)**: `arduino_core_ch32_riscv_arduino` 1.4.0は**案(a)を実際に採用**していた。同梱`api/Print.h`に`Print::vprintf`+`printf`×2 overload(約50行、ESP32系と同じ実装)をpatchし、`Print.h.orig`をバックアップとして残している。全メソッドがclass内inline定義のため**未使用時コストはゼロ**。詳細は[legacy-audit 調査対象B](legacy-audit.ja.md)。


## Toolchain

| ID | 優先度 | 論点 | 判断に必要なもの |
|---|---:|---|---|
| Q-021 | P0 | interrupt ABIとWCH固有高速割込みの扱い | disassembly、register preservation、latency HIL。静的部分(attribute生成コード、fork属性の非互換)は実験0008で確認済み。latency実測が残り |
| Q-024 | P1 | LTOをdefaultにするか | weak ISR、archive、debug、size、link再現性。weak ISR/vector/constructorのLTO無害は実験0008で確認。size/debug/再現性比較が残り |
| Q-025 | P2 | WCH toolchain compatibility laneを維持するか | 固有命令の効果と維持コスト |

## Vendorとライセンス

| ID | 優先度 | 論点 | 判断に必要なもの |
|---|---:|---|---|
| Q-030 | P0 | WCH EVT/header/sourceの再配布・改変条件 | 各file notice、repository条件、必要ならWCHの書面回答 |
| Q-031 | P0 | vendorから取り込む最小ファイル集合 | startup/device headerをown実装と比較。startupはADR-0003でown化し対象外。残りはdevice header/SPL等 |
| Q-032 | P1 | 旧33 patchのうち再現する不具合 | 新toolchainでのcompile/runtime regression test |
| Q-033 | P2 | SBOM formatとrelease notice | Board Manager配布物、依存tool一覧、CI生成方法 |
| Q-034 | P0 | vendor lockのhash正本 | Git commit、取得archive hash、canonical tree、allowlist file hashの比較 |

## Uploadとfixture

| ID | 優先度 | 論点 | 判断に必要なもの |
|---|---:|---|---|
| Q-040 | P0 | `probe-rs 0.32.x`をprimary backendにできるか | 対象SKU×WCH-Link FW×host OSのflash/verify/reset試験 |
| Q-041 | P0 | 最初のLinux HIL runnerで書き込み先を一意に選択する方法 | topology列挙だけでなくbackendが指定deviceだけをopenするprototype。PPPSによる他port切断は選択方法にしない。**前提として「LinkE複数台の同時接続不可」の原因切り分けが必要**(Q-043) |
| Q-042 | P0 | target UIDのaddressと保護時挙動 | device別reference manualと実機read試験 |
| Q-043 | P1 | 1 lane 1 LinkEか、共有LinkE+muxか | fixture費用、throughput、信号品質、故障解析比較。**maintainer報告(2026-08-19): LinkEは複数所有だが同時接続不可**のため共有+mux側へ傾く。原因(serial重複かtool制限か)の切り分けが先 |
| Q-044 | P1 | uploader frontendを独自binaryにするかwrapperにするか | Arduino packaging、cross-platform配布、probe-rs API安定性。**maintainer報告(LinkE複数台の同時接続不可)により昇格条件に接近**。原因切り分け実験は[upload-and-fixture](upload-and-fixture.ja.md)に記載 |
| Q-045 | **P1?** | ESP32系/RP2040 programmerを開発するか | 対象protocol、unique IDと複数台選択、全family対応工数、既存実装との比較。Q-041を既存toolで解決できなければ優先度を上げる。**LinkE複数台の同時接続不可という報告が出たため、切り分け結果次第でP1へ昇格** |
| Q-046 | P2 | flash失敗、保護、電源断からの復旧contract | fault injectionとbrick/recovery試験 |
| Q-047 | P2 | Windows/macOSでの物理probe選択 | 対応OS確定後のUSB topology、driver、backend prototype |
| Q-048 | P0 | WCH-Link firmwareの固定・更新方針 | 型番、HW revision、mode、FW別backend認定とrollback試験 |
| Q-049 | P1 | 新規toolのrepository、CLI/API、release単位 | Arduino、`ch32fun`、単体利用から共用できるprototypeとversioning |

## Testとrelease

| ID | 優先度 | 論点 | 判断に必要なもの |
|---|---:|---|---|
| Q-050 | P0 | 初期fixtureのLA channel/pin、adapter connector、電源構成 | 選定boardのpinmux、8chでのtest case割当、電圧、配線prototype |
| Q-051 | P1 | code/RAM size budget | empty/Blink/Serial baselineと旧/公式/ch32fun比較。newlib系baselineは実験0006、Blink 26 SKUはsizes_baseline.jsonでgate済み。閾値の正式化が残り |
| Q-052 | P1 | timing toleranceの決め方 | oscillator条件、sample rate、複数個体測定 |
| Q-053 | P1 | HIL runnerの信頼境界 | CI provider、artifact署名、fork PR policy |
| Q-055 | P2 | 対応OS matrix | 利用者需要とtool/programmer artifactの提供可能性 |
| Q-056 | P1 | replay corpusとartifact保持方針 | decoder固定、golden昇格review、保持期間、storage cost |
| Q-057 | P0 | fixture healthとcandidate failureの境界 | 独立self-test、既知good firmware、candidate READY failureの試験 |

## 実験結果の残し方

各spike/実験では次を残します。

- 対象question ID
- hardware型番、package、silicon revision
- programmer型番とfirmware
- tool version/commitとcommand
- wiring、clock、power条件
- firmware source/ELF hash
- raw log、map/disassembly、logic analyzer capture
- 結論と再現手順
- ADRへ進めるか、追加実験が必要か

重要な決定は[ADR](adr/README.ja.md)へ移し、この一覧にはADRへのlinkを追加します。

## 解決済み

**注記(2026-08-19)**: 下表の「結論」はADR-0001〜0008を根拠にしていますが、これらは同日すべて`Status: Proposed`へ戻しました([承認プロセス](adr/README.ja.md))。したがって現時点で確定しているのは**論点の整理と有力案**であり、maintainer承認は未了です。

| ID | 結論(いずれもADR承認待ち) |
|---|---|
| Q-018 | device databaseの正本を独立`ch32-device-data` repositoryに置く。[ADR-0001](adr/0001-device-data-repository.ja.md)。releaseとconsumer lock形式はQ-014で継続する |
| Q-012 | startup/vector/linkerはowned実装。共通crt0+family別vector include(将来device-data生成)。[ADR-0003](adr/0003-owned-startup-vector-linker.ja.md) |
| Q-015 | 開発用暫定ID: packager=`ch32-riscv-ug`、architecture=`ch32v`。boardはfamily単位+pnum全型番。[ADR-0005](adr/0005-board-structure-and-fqbn.ja.md) |
| Q-020 | xPack riscv-none-elf-gccのGitHub Releases直リンク参照。認定候補14.3.0-1。ch32fun比較はrelease前validationとして残る。[ADR-0002](adr/0002-toolchain-distribution.ja.md) |
| Q-022 | default=newlib-nano。printf `%f`はmenu opt-in。ltoa/ultoa/dtostrfはcore提供。[ADR-0004](adr/0004-runtime-and-cxx.ja.md) |
| Q-023 | GNU++17(+-fno-exceptions/-fno-rtti/-fno-threadsafe-statics)。サイズ差ゼロを実測確認。[ADR-0004](adr/0004-runtime-and-cxx.ja.md) |
| Q-026 | 直リンク参照(convey回避)で満たす。再ホスト時はGPL§6(d)対応へ切替。[ADR-0002](adr/0002-toolchain-distribution.ja.md) |
| Q-006 | コアはベアメタル単一セマンティクス。初期リリースはRTOSなし。将来はコア同梱FreeRTOSライブラリ(UNO R4方式)を第一候補とし、提供開始は利用者需要で判断。[ADR-0006](adr/0006-rtos-policy.ja.md) |
| Q-054 | コアが1つの間は本repositoryから直接配信し、同一名前空間のコアが増えたらlang-ship方式(統合index repo+release kick)へ移行する(2026-08-19決定)。append-only運用の詳細はrelease前に確定 |
