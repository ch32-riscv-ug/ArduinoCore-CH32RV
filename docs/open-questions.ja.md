# 未決定事項

文書基準日: 2026-08-17

優先度は`P0`が実装開始前、`P1`が最初のvertical slice中、`P2`が初期release前です。

## Scopeと互換性

| ID | 優先度 | 論点 | 判断に必要なもの |
|---|---:|---|---|
| Q-001 | P0 | 最初に正式対象とするexact SKU/package/board | 所有実機、一次資料、fixture配線、利用者需要 |
| Q-002 | P0 | 初期releaseをRISC-V CH32だけに限定するか | CH32F対応の需要とrepository/package構造への影響 |
| Q-003 | P1 | Arduino APIの初期対応範囲 | ArduinoCore-API inventory、サイズbudget、利用例 |
| Q-004 | P1 | 旧コアとのソース互換範囲 | 既存sketch/exampleのcompile survey |
| Q-005 | P2 | EVT Compatibility Packをcore releaseへ同梱するか別artifactにするか | Arduino Builder挙動、versioning、license、更新頻度 |

## Architectureとmanifest

| ID | 優先度 | 論点 | 判断に必要なもの |
|---|---:|---|---|
| Q-010 | P0 | `ArduinoCore-API`の固定versionと取込方法 | サイズ試験、LGPL配布方法、symlinkを使わないrelease方法 |
| Q-011 | P0 | device/board manifestのformatとschema実装 | 最初の2 deviceを表現するprototype |
| Q-012 | P0 | startup/vector/linkerを手書き、template生成、外部data生成のどれにするか | family差分比較、ELF検査、debug互換性 |
| Q-013 | P1 | 内部HAL contractをどこまで設けるか | digital/time/Serial/SPI/I2Cの2 family実装比較 |
| Q-014 | P1 | `ch32-data`を入力に使うか | license/provenance監査、最新RMとの自動比較 |
| Q-015 | P0 | 開発用の暫定packager/architecture/FQBN | Arduino CLIでvertical sliceをbuildできる最小platform prototype |
| Q-016 | P0 | host contract testを何で実行するか | host-arduino-core固定利用、内部HAL mock、native unitの比較 |
| Q-017 | P2 | 公開用FQBN、packager ID、architecture ID | Arduino package互換性、既存公式coreとの衝突確認、暫定IDからの移行 |

## Toolchain

| ID | 優先度 | 論点 | 判断に必要なもの |
|---|---:|---|---|
| Q-020 | P0 | default GCC distribution/version | RV32E/ILP32E、RV32I、FPU、host OS、`ch32fun`比較の認定結果 |
| Q-021 | P0 | interrupt ABIとWCH固有高速割込みの扱い | disassembly、register preservation、latency HIL |
| Q-022 | P0 | newlibまたは代替runtimeの構成 | printf、constructor、code/RAM size、license |
| Q-023 | P1 | C++ standardをGNU++11/14/17のどれにするか | Arduino library compile matrix、サイズ比較 |
| Q-024 | P1 | LTOをdefaultにするか | weak ISR、archive、debug、size、link再現性 |
| Q-025 | P2 | WCH toolchain compatibility laneを維持するか | 固有命令の効果と維持コスト |
| Q-026 | P0 | toolchain artifactの取得・改変・再配布条件を満たせるか | component別license、対応source、patch、公式配布元、host別archive |

## Vendorとライセンス

| ID | 優先度 | 論点 | 判断に必要なもの |
|---|---:|---|---|
| Q-030 | P0 | WCH EVT/header/sourceの再配布・改変条件 | 各file notice、repository条件、必要ならWCHの書面回答 |
| Q-031 | P0 | vendorから取り込む最小ファイル集合 | startup/device headerをown実装と比較 |
| Q-032 | P1 | 旧33 patchのうち再現する不具合 | 新toolchainでのcompile/runtime regression test |
| Q-033 | P2 | SBOM formatとrelease notice | Board Manager配布物、依存tool一覧、CI生成方法 |
| Q-034 | P0 | vendor lockのhash正本 | Git commit、取得archive hash、canonical tree、allowlist file hashの比較 |

## Uploadとfixture

| ID | 優先度 | 論点 | 判断に必要なもの |
|---|---:|---|---|
| Q-040 | P0 | `probe-rs 0.32.x`をprimary backendにできるか | 対象SKU×WCH-Link FW×host OSのflash/verify/reset試験 |
| Q-041 | P0 | 最初のLinux HIL runnerでWCH-Linkを物理pathから選択する方法 | topology列挙だけでなくbackendが指定deviceをopenするprototype |
| Q-042 | P0 | target UIDのaddressと保護時挙動 | device別reference manualと実機read試験 |
| Q-043 | P1 | 1 lane 1 LinkEか、共有LinkE+muxか | fixture費用、throughput、信号品質、故障解析比較 |
| Q-044 | P1 | uploader frontendを独自binaryにするかwrapperにするか | Arduino packaging、cross-platform配布、probe-rs API安定性 |
| Q-045 | P2 | ESP32-S3 programmerを開発するか | 対象protocol、全family対応工数、既存S2実装との比較 |
| Q-046 | P2 | flash失敗、保護、電源断からの復旧contract | fault injectionとbrick/recovery試験 |
| Q-047 | P2 | Windows/macOSでの物理probe選択 | 対応OS確定後のUSB topology、driver、backend prototype |
| Q-048 | P0 | WCH-Link firmwareの固定・更新方針 | 型番、HW revision、mode、FW別backend認定とrollback試験 |

## Testとrelease

| ID | 優先度 | 論点 | 判断に必要なもの |
|---|---:|---|---|
| Q-050 | P0 | 最初のreference fixture構成 | 選定board、programmer、UART、logic analyzer、power controller |
| Q-051 | P1 | code/RAM size budget | empty/Blink/Serial baselineと旧/公式/ch32fun比較 |
| Q-052 | P1 | timing toleranceの決め方 | oscillator条件、sample rate、複数個体測定 |
| Q-053 | P1 | HIL runnerの信頼境界 | CI provider、artifact署名、fork PR policy |
| Q-054 | P2 | Board Manager indexを別repositoryにするか | release権限、append-only運用、beta/stable導線 |
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
