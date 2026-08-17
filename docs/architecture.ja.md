# アーキテクチャ案

文書状態: 提案

## 全体像

```text
Sketch / Arduino libraries
            │
            ▼
 ArduinoCore-API（固定版、原則無改変）
            │
            ▼
 CH32 Arduino adapters
 digital / time / Serial / SPI / Wire / analog / interrupts
            │
            ▼
 非公開の小さな内部HAL contract
            │
            ▼
 soc/<family> + device/package descriptor
            │
            ▼
 owned startup / CRT / vector / linker
            │
            ▼
 必要最小限のvendor由来header/source
```

通常のArduino利用は上記の経路だけで完結させ、EVT APIやEVT sampleを要求しません。低レベル挙動はdatasheet/reference manualを一次情報とし、EVTと`ch32fun`を相互確認に利用します。EVTだけで仕様や実用実装が完結するとは仮定しません。

将来、実用的なArduino APIとexampleを提供した後にも明確な需要があり、利用・再配布条件を満たせる場合だけ、次の互換packを独立artifactとして検討します。初期releaseの構成要素ではありません。

```text
EVT Compatibility Pack
  ├─ version固定したvendor C
  ├─ family別compat header/define
  ├─ サンプル別source manifest
  └─ compile/HIL済みexample catalog
```

## レイヤーの責務

### Arduino API

- `ArduinoCore-API`の固定revisionを利用する案を第一候補とする
- `String`、`Print`、`Stream`などhardware非依存部分を独自forkしない
- 本プロジェクト側はhardware依存interfaceを実装する
- CH32V003級の小容量デバイスで、未使用機能がdead stripされることをサイズ試験する

公開APIは次の優先順位で設計します。

1. Arduinoの標準API、signature、意味
2. 固定する`ArduinoCore-API`のcontract
3. ESP32等、複数の主要Arduino coreで利用者に定着した拡張方法
4. 上記で表現できない場合に限るCH32固有拡張

CH32V003級ですべての機能を実装できない場合でも、標準APIの意味を別の意味へ変更しません。SKUごとの対応範囲と制限をcapabilityとして公開し、未対応機能を成功したように見せる空実装にはしません。compile-time error、runtime error、機能省略のどれで表現するかはAPIごとに決定し、support matrixとexampleへ反映します。

拡張は標準APIと名前やoverloadで衝突させず、標準的なsketchが拡張をincludeしなくてもbuildできる構造にします。主要coreの公開APIと挙動は比較対象であり、そのsourceを無断でコピーするものではありません。

### Arduino adapters

- `pinMode`、`digitalWrite`、`millis`など公開APIの意味を実装する
- vendor headerやvendor型をpublic signatureへ出さない
- pin、timer、serial instanceの解決は生成されたdescriptorを利用する

### 内部HAL

巨大な汎用SDKを新しく作ることは目的ではありません。Arduino adaptersがfamily差を吸収するための、小さな非公開contractだけを定義します。

- GPIO mode/read/write
- monotonic clock/tick
- UART、SPI、I2Cのinstanceとpin routing
- ADC/PWM/timerの基本操作
- interrupt attach/detachとcritical section
- clock/reset gating

外部ライブラリに安定APIとして公開する必要が生じた場合は、Arduino APIと分けてADRで決定します。

### SoC/device/board

少なくとも次の概念を区別します。WCHの型番体系を調査し、manifest上でどこまで別entityにするかは最初の2機種で決定します。

- `family`: 共通するcore、register layout、interrupt model
- `die/variant`: silicon構成、memory選択、peripheral、UID、vector
- `package/orderable SKU`: package、bond-out pin、注文可能な正確な型番
- `board`: 搭載SKU、clock source、connector/pin mapping、LED、既定bus、upload設定

想定するmanifest例です。

```yaml
id: ch32v003f4p6
family: ch32v00x
cpu:
  march: rv32ec
  mabi: ilp32e
memory:
  flash_bytes: 16384
  ram_bytes: 2048
capabilities:
  - gpio
  - adc
  - usart
support:
  tier: experimental
  evidence: []
```

値は例であり、schemaと一次資料で再検証してから確定します。

manifestから次を生成する案です。

- `boards.txt`
- `variants/`
- pin tables
- linker scriptsまたはlinker parameter
- compile defines
- CI matrix
- 対応表
- package metadataのboard一覧

生成物はリポジトリへ格納し、CIで`generate --check`相当を実行します。利用者のビルド時にgeneratorを要求しません。

## runtimeの所有権

本プロジェクトが次を所有します。

- reset entry
- data/BSS初期化
- `SystemInit`を含むclock初期化との順序
- C/C++ constructor初期化
- weak/default interrupt handler
- vector table
- linker layout
- newlib syscallの最小実装
- Arduinoの`setup()`/`loop()`入口

旧EVT startupへfamilyごとのpatchを当てる方式は使用しません。constructor、global/static初期化、weak override、interrupt ABIはcompile testとHILで検証します。

## C/C++境界

- vendorの`.c`はCとしてコンパイルする
- `Arduino.h`はvendorの`debug.h`や全Peripheral headerをincludeしない
- C++から必要な低レベル機能だけを、小さな`extern "C"`境界で公開する
- own codeでは`-fpermissive`を使わない
- vendor警告抑制が必要な場合、vendor translation unitだけへ限定する

## EVT Compatibility Pack

文書状態: 将来の条件付き候補。初期release対象外。

互換packでは、旧コアの次の仕組みを限定的に継承できます。

- core側のweak `main()`
- EVT sampleのstrong `main.c`
- Arduino Builderに認識させる最小の`.ino`
- EVTソースをCとしてコンパイル

各exampleは必要source、include、define、対象deviceをmanifestへ列挙します。EVTの全`User`ディレクトリを自動収集しません。RTOS、USB、networkなど追加条件があるexampleは明示的に分類します。

strong `main()`を許しても、runtime全体の所有権は変更しません。

| 項目 | 通常Arduino mode | EVT example mode |
|---|---|---|
| reset/vector/linker | core | core |
| data/BSS、constructor | core | core |
| baseline `SystemInit`/clock | 選択されたSoC runtime | 選択されたSoC runtime |
| `main` | coreのweak入口 | sampleのstrong `main.c` |
| `setup`/`loop` | coreの`main`から呼ぶ | 原則未使用 |
| application peripheral初期化 | sketch/library | EVT sample |

EVT sampleが独自startup、vector、linker、重複する`SystemInit`を持つ場合は、manifestで除外または明示的に置換し、暗黙に同時linkしません。

## 外部SDKとの互換範囲

外部SDKの周辺機能コードは利用可能にしたい一方、複数のruntime所有者を同時に許すと破綻します。

- Arduino coreがstartup、linker、clock、vectorを所有する通常mode
- EVT sampleのstrong `main()`を許す明示的compatibility mode

この2つを区別します。ch32funや独自SDKを利用する場合も、startupやsystem初期化を二重にlinkしないためのcontractが必要です。

## 想定するリポジトリ構成

```text
ArduinoCore-CH32/
  boards/
  devices/
  cores/ch32/
    api/
    core/
    hal/
    runtime/
    soc/
  variants/
  libraries/
  compat/evt/
  vendor/
  tools/
    generate/
    vendor-sync/
  tests/
    unit/
    host/
    compile/
    hil/
    replay/
  ci/
  docs/
```

実装開始前にArduino Builderの探索規則と照合し、不要な階層やビルド対象の混入がないことを確認します。
