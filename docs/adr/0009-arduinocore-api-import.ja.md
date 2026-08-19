# ADR-0009: ArduinoCore-APIはtag 1.5.2をvendored snapshotとして取り込む

- Status: Proposed
- Date: 2026-08-19
- Related questions: Q-010, Q-016, Q-031, Q-033, Q-034

## Context

コア本体の公開APIは`ArduinoCore-API`の固定revisionを原則無改変で使う方針([architecture](../architecture.ja.md))。[実験0007](../experiments/0007-arduinocore-api-target-build.ja.md)でcommit `0f4e57e`が対象最小構成(rv32emc_zicsr/ilp32e、GNU++17、newlib-nano)で警告ゼロcompileできることと、coreが補う不足シンボル(`ltoa`/`ultoa`/`dtostrf`)を確認済み。残る論点は**versionの固定方法**と**LGPL-2.1配布の満たし方**の2点。

制約として次がある。

- [vendor-policy](../vendor-policy.ja.md): build時にvendor archiveをdownloadしない / snapshotをリポジトリへ格納しoffline buildを可能にする / source URL・commit・SHA-256を記録する
- Board Manager配布物にsymlinkを含めない(Windows展開とarchive移植性)
- リポジトリルートはMIT。ArduinoCore-APIはLGPL-2.1-or-later

本ADRは法的判断を行うものではない。

## Decision drivers

- `git clone`単体で完結し、offline buildができること(vendor-policy)
- symlinkなしでrelease archiveを作れること
- 取り込んだtreeとupstreamのbyte一致を機械検証できること
- LGPL-2.1-or-laterの条件を配布物で満たし、MITのown codeとのlicense境界が明確なこと
- upstream更新をreview可能なPRにできること

## 調査結果(2026-08-19時点の事実)

| 項目 | 値 |
|---|---|
| upstream | [arduino/ArduinoCore-API](https://github.com/arduino/ArduinoCore-API) |
| 最新tag | `1.5.2` = `cd91833d90b4fe50e428021ba5051e2b7ceafc84`(2025-09-29) |
| 実験0007のcommit | `0f4e57ea193a00163ae59f0f0ff478feae7eb5db`(2025-10-12、当時のmaster HEAD) |
| 両者の`api/` tree hash | **同一** `e1223bd76ddcb72801f3a6509e4bcb5ca311294c` |
| 差分 | `README.md`の2行(donation link削除)のみ |
| `api/`規模 | 46ファイル / 308KB、`.cpp`は8本。`deprecated/`と`deprecated-avr-comp/`はheaderのみ |
| version macro | `api/ArduinoAPI.h`に`#define ARDUINO_API_VERSION 10502` |
| license header | 46中44ファイルがLGPL-2.1-or-laterヘッダ付き。`api/Udp.h`と`api/deprecated-avr-comp/avr/pgmspace.h`は無し(repository LICENSEでカバー) |

実験0007は「tagなし」と記録したが、**tag `1.5.2`の`api/`は実験0007のcommitと完全に同一**であり、tagで固定しても実測結果はそのまま有効である。

### 前コアで起きていた版のずれ(本ADRの動機)

旧`arduino_core_ch32_riscv_arduino` 1.4.0は`libraries/ArduinoCoreAPI/src/api/`にupstreamをbundleしているが、実測すると:

- `ArduinoAPI.h`は`ARDUINO_API_VERSION 10501`(=1.5.1)と宣言している
- しかし内容は1.5.1ではない。1.5.1以降のlicense header追加が入っており、1.5.2で追加された`SPIBusMode`は入っていない
- 実体は`1.5.1..1.5.2`間の**untagged master snapshot**(16 commit中9 commitと一致し、それ以上は特定できない)

**版マクロが実際の内容と一致せず、どのrevisionを配っているのか事後に特定できない**状態になっていた。本ADRがtag固定・tree hash記録・CIでのbyte一致検証を求めるのは、この失敗を繰り返さないためである(詳細は[legacy-audit 調査対象B](../legacy-audit.ja.md))。

他coreの取込方式:

- `ArduinoCore-renesas`: `cores/arduino/api` は `../../../ArduinoCore-API/api/` へのsymlink(兄弟checkout前提。submoduleですらない)
- `Arduino_Core_STM32`: `cores/arduino/api` はgit submodule
- `ArduinoCore-samd` / `-mbed` / `-zephyr` / `arduino-pico`: `cores/arduino/api`を持たない

## Options considered

### Option A: 兄弟checkoutへのsymlink(renesas方式)

利点: upstreamのcopyがリポジトリに一切入らない。
欠点: `git clone`単体でbuildできない。Windowsでのsymlink展開とrelease archiveの制約に正面から反する。**不採用**。

### Option B: git submodule(STM32duino方式)

利点: 履歴上のpinがcommit SHAで自明、更新がsubmodule bump 1行、upstream codeを自repoへ複製しない。
欠点: `--recursive`忘れでbuild不能。ZIP download / `git archive` / GitHub release tarballから消えるため、release packagingで結局実体化(copy)工程が要る。vendor-policyの「snapshotを格納しoffline buildを可能にする」に反する。**不採用**。

### Option C: `libraries/`配下にbundleする(前コア方式)

前コア`arduino_core_ch32_riscv_arduino` 1.4.0は`libraries/ArduinoCoreAPI/`に置いていた。
利点: platform bundled libraryとして扱われ、includeされたときだけcompileされる。
欠点: `Arduino.h`がplatform libraryへ依存する構造になり、コアの必須要素がlibrary解決順序に左右される。実測では`cores/`配置でも`--gc-sections`が未使用分を完全に除去するため(26 SKUでBlinkのサイズがbaselineとバイト一致)、この利点は実質存在しない。**不採用**。

### Option D: vendored snapshot + lock manifest + CI byte一致検証(採用)

利点: `git clone`もZIPもrelease tarballも同一構造で完結。symlink不要。既存の`generated-sync` job(device-data locked commit検証)と同型の仕組みで済み、CI/運用パターンが増えない。vendor-policyの記述通り。
欠点: MITリポジトリにLGPLのsub-treeが入るため、license境界の明示が必須。upstream追随が自動でなく明示PRになる。

## Decision

1. **固定versionはtag `1.5.2`**(commit `cd91833d90b4fe50e428021ba5051e2b7ceafc84`)。実験0007の実測はこのtreeに対して有効。coreの共通headerで`static_assert`相当の`#if ARDUINO_API_VERSION != 10502` guardを置き、意図しない差替えをcompile errorにする
2. upstreamの`api/`ディレクトリを**無改変**で`cores/arduino/api/`へ格納する(`build.core=arduino`のため実パス)。改変が必要になった場合はvendor-policyのpatch手順で管理し、本ADRを更新する
3. upstreamの`test/`、`.github/`等は取り込まない。Q-016のhost contract testで必要になった場合は、CIが同一の固定commitでcloneして使う(配布物には入れない)
4. lock manifestを`vendor/arduino-core-api.lock.toml`に置き、url / tag / commit / `api/` tree hash / 各ファイルSHA-256 / verified_date / license(`LGPL-2.1-or-later`)を記録する
5. CIに`api-sync` jobを追加する。固定commitでcloneし`cores/arduino/api/`とのbyte一致を検証する(既存`generated-sync`と同型、全PR実行)
6. LGPL-2.1配布は次で満たす。
   - `cores/arduino/api/LICENSE`にupstreamのLGPL-2.1全文を置き、ルートMITの適用外であることをREADMEとlock manifestに明示する
   - 各ファイルのcopyright/license noticeを保持する(無改変のため自動的に満たす)
   - **releaseはsource配布のみとし、coreのprecompiled library/binaryを配布しない**(ADR-0006の「同梱ライブラリをプリコンパイル配布にしない」と同じ方針)。利用者のsketch binaryは利用者のhostでlinkされる
   - release artifactに第三者inventory(Q-033のSBOM)を含め、upstream URLとcommitを記載する
7. **upstream追随は積極的に行わない。** ArduinoCore-APIは変更頻度が低く、追随自体に価値はないため、定期bot PRは設けない。必要になったとき(欲しい修正/APIが入った、compile不能になった等)にだけ手動PRで上げる。その際は本ADRの固定versionと`ARDUINO_API_VERSION` guardを同一PRで更新し、compile matrixとsize baseline(W-7)をgateにする

## Consequences

- **本ADRは取込形態のみを決める。** コア拡張(例: `Serial.printf()`)を`api/`改変で実現するか、派生クラス等の別手段で実現するかは**未決定**(Q-018)。`api/`を改変する選択を採る場合、Decision 2/5のbyte一致検証はpatch適用後treeとの比較に組み替える必要がある

- 得るもの: `git clone`だけで完結するoffline build、symlinkなしrelease、upstreamとのbyte一致の常時証明、SBOM生成の単純化
- 失うもの: リポジトリに308KBのLGPL sub-treeが入る。license境界の説明責任が生じる。upstream更新の追随が明示作業になる
- renesas/STM32duinoからの移植手順ではpathは同じ(`cores/arduino/api`)だが実体の持ち方が違う点をCONTRIBUTINGへ記載する
- [architecture](../architecture.ja.md)の想定構成は`cores/ch32/api/`と書いているが、実構成は`build.core=arduino`に合わせ`cores/arduino/api/`とする(ADR-0005の生成boards.txtと整合)

## Validation

- CI `api-sync`: pinned commitからのcloneと`cores/arduino/api/`のbyte一致(全PR)
- `#if ARDUINO_API_VERSION != 10502`による固定version guard
- W-7 size baselineがapi更新時のサイズ回帰を検出
- license lint: `cores/arduino/api/`配下の全ファイルがlicense headerまたは同梱LICENSEでカバーされること

## References

- [実験0007](../experiments/0007-arduinocore-api-target-build.ja.md)
- [ecosystem](../ecosystem.ja.md)、[vendor-policy](../vendor-policy.ja.md)、[architecture](../architecture.ja.md)
- [ADR-0004](0004-runtime-and-cxx.ja.md)(GNU++17とnewlib-nano)、[ADR-0006](0006-rtos-policy.ja.md)(precompiled配布しない方針)
- upstream: [arduino/ArduinoCore-API](https://github.com/arduino/ArduinoCore-API) tag `1.5.2`
