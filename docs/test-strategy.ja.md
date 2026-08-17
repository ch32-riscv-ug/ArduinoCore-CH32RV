# テスト戦略

文書状態: 提案

## 方針

安価で速いtestを毎PRで実行し、実機でしか分からない問題だけをHILへ送ります。

```text
static/schema
      ↓
native unit / host contract
      ↓
compile matrix
      ↓
flash / boot / Serial smoke
      ↓
logic analyzer conformance
      ↓
full release qualification
```

## Testレイヤー

| レイヤー | 内容 | 主な実行契機 |
|---|---|---|
| Static | schema、生成物、format、license inventory | 全PR |
| Unit | pin変換、clock計算、ring buffer等の純粋C/C++ | 全PR |
| Host | Arduino API contract、境界値、error path | 全PR |
| Compile | 代表FQBN、example、C/C++互換、サイズ | 全PR |
| HIL smoke | flash、boot、Serial、GPIO、timer | main、承認済みPR |
| HIL conformance | PWM、UART、SPI、I2C、interrupt波形 | nightly、release |
| Replay | 保存済みlogic analyzer captureの再decode | 全PR |
| Package | Board Manager install、複数OS | release候補 |

## Source of truth

boardとCI matrixをworkflowへ直接重複記載しません。例えば`ci/targets.yaml`に次を持たせます。

```yaml
targets:
  - board: <board-id>
    fqbn: <generated-fqbn>
    support_tier: experimental
    compile_groups: [pr, nightly]
    fixture: null
    required_features: [gpio, serial]
```

新しいboardがいずれのcompile groupにも属さない場合や、Tier Aなのにrelease fixtureがない場合はschema testを失敗させます。

## Sketch testの単位

[`host-arduino-core`](https://github.com/tanakamasayuki/host-arduino-core)を参考に、1 caseを1 sketch directoryとします。

```text
tests/sketches/<category>/<case>/
  <case>.ino
  sketch.yaml
  build_config.toml
  test_<case>.py
```

同じsketchをhost profileとCH32 profileで動かせる場合、公開APIのconformance testとして利用します。

公開API contractを比較するtestでは、`#ifdef HOST`や`#ifdef CH32`によって期待動作そのものを分岐させないことを原則とします。fixture接続や観測方法の違いはtest harness側で吸収します。

testはユーザーの通常のArduino data/sketchbook directoryを変更せず、一時directoryと専用Arduino CLI configurationを使います。

### Host test方式は未決定

`host-arduino-core`を固定versionで利用する、CH32内部HALのmock backendを作る、純粋native unitだけに限定する、の選択肢があります。全PRで要求するhost contractの実体は、最初のvertical slice前に決定します。

## HIL protocol

[`I2CDeviceDB`](https://github.com/tanakamasayuki/I2CDeviceDB)の方式を一般化します。

```text
Host -> READY?
DUT  -> READY {build_id, board_id, capabilities}
Host -> logic analyzer arm
Host -> RUN {case_id, parameters}
DUT  -> EVENT {...}
DUT  -> DONE {status, measurements}
Host -> capture stop and flush
```

DUTはboot時に一度だけbannerを出すのではなく、`READY?`へ繰り返し応答します。測定対象の動作は`setup()`直後ではなく、logic analyzerをarmした後の`RUN`で開始します。

`READY`はcandidate coreのruntimeとSerialへ依存します。programmer/analyzer/controllerの独立health check、既知good診断firmwareによるfixture self-test、candidate firmwareのconformance testを区別します。candidateを書いた後の`READY`失敗をfixture failureとしてskipしません。

## Logic analyzer

sigrok対応analyzerを第一候補とし、物理channel名をtestへ書きません。

```text
D0 -> MARKER
D1 -> GPIO_OUT
D2 -> UART_TX
D3 -> SPI_SCK
```

testとdecoderは`MARKER`、`GPIO_OUT`等の論理名だけを扱います。

小容量deviceでmarker専用UART/GPIOを確保できない場合は、制御UARTをlogic analyzerへ分岐するか、scenario metadataと最小の同期edgeへ縮退します。この縮退時にも測定開始点を一意に決められることをtestします。

判定を次の2層へ分けます。

- functional signature: byte列、edge順、transaction、interrupt順序
- timing result: frequency、duty、pulse width、timeoutを許容範囲で判定

raw captureのexact hashだけを合否に使いません。raw `.sr`を保存し、decoder変更後もCIでreplayします。

## 初期HIL項目

- flash/verify/reset
- global constructorと`setup()/loop()`到達
- GPIO level/toggle
- `millis`、`micros`、`delay`、wraparound近傍
- Serial baud/bytes/buffer
- PWM frequency/duty
- external interruptの順序と基本latency
- SPI mode、bit order、clock
- I2C address、read/write、NACK、bus recovery
- ADC loopbackまたは既知電圧

passiveなlogic analyzerだけでは入力側APIを検証できません。fixture controllerまたは専用peerから、GPIO edge、既知電圧、UART/SPI/I2C transaction、NACK、bus stuck等の刺激を与えます。

`millis()`等のwraparoundは実時間で待たず、test-only counter seed、clock backend差替え、またはnative unitで境界直前へ進めます。test seamがrelease buildへ残らないことも確認します。

capabilityを次のように分離します。

- siliconが持つhardware capability
- package/boardで利用可能なpin/resource
- coreが実装すると宣言したfeature
- fixtureが観測・刺激できるfeature
- support tierが要求するtest

siliconに機能がない場合はskipできますが、coreが実装を宣言しsupport tierが要求するtestの欠落はfailにします。「未実装だからcapabilityを外してskip」は許可しません。

## Artifact

各runで次を共通run IDの下へ保存します。

- compile/link/size log
- ELF、BIN、map file
- flash/verify log
- Serial transcript
- JUnit/HTML report
- raw `.sr`
- decoded JSONLとmeasurement summary
- core commit、toolchain、FQBN/options、sketch hash、firmware hash
- fixture ID、programmer backend/FW、analyzer sample rate、power条件

versionやFQBNを手書きで複製せず、実際のbuild metadataからprovenanceを生成します。

raw captureを含む全artifactを無期限保存しません。成功run、失敗run、release evidence、golden replay corpusの保持期間を分けます。goldenへ昇格するcaptureはレビューし、content-addressedに保存します。sigrok/libsigrok/decoder versionまたはcontainer imageも固定します。

## CI安全性と排他

- HILはfixture ID単位でfile lockとCI concurrencyを設定する
- 未信頼fork PRのfirmwareを常設hardware runnerへ直接書き込まない
- hosted CIでcompileし、HILは信頼済みbranchまたは明示的に承認されたartifactだけを使用する
- fixture health failureとcore conformance failureを別の結果にする
- teardownでpower、capture、Serial、lockを確実に解放する

## 最初の縦切り

最初のHIL完了条件は、1つの正確なboardについて次が自動で通ることです。

1. clean environmentでcompile
2. 一意なfixtureへflash/verify
3. reset後に`READY`を受信
4. logic analyzerをarm
5. `RUN`後のGPIO pulse数と周期を判定
6. build、flash、Serial、raw captureをartifact化

これを完成させてから、device数と周辺機能を増やします。
