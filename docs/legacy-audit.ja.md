# 旧コア監査

旧コアは**2つ**あります。当初の監査(2026-08-17)は`_noneos`だけを対象にしていました。実際にArduino APIを実装しているのは`_arduino`のほうで、こちらは2026-08-19に追加監査しました。

| repository | 位置づけ | 監査 |
|---|---|---|
| [`arduino_core_ch32_riscv_noneos`](https://github.com/ch32-riscv-ug/arduino_core_ch32_riscv_noneos) | EVT sampleをArduino IDEから使うためのgenerator/overlay | 本書「調査対象A」(2026-08-17) |
| [`arduino_core_ch32_riscv_arduino`](https://github.com/ch32-riscv-ug/arduino_core_ch32_riscv_arduino) | **実際にArduino APIを実装**。`ch32-riscv-arduino:ch32riscv` 1.4.0として配信中 | 本書「調査対象B」(2026-08-19) |

## 調査対象A: arduino_core_ch32_riscv_noneos

- Repository: [`ch32-riscv-ug/arduino_core_ch32_riscv_noneos`](https://github.com/ch32-riscv-ug/arduino_core_ch32_riscv_noneos)
- Commit: `b4e91720ae4c0ebf443db170c06e122c6acd15a7`
- Describe: `1.4-2-gb4e9172`
- 調査日: 2026-08-17

## 旧リポジトリの実体

旧リポジトリはArduinoコア本体を直接管理していません。WCH EVTを取得し、familyごとのコアへ加工してBoard Manager用ZIPを作成するgenerator/overlayです。

名称はArduinoコアですが、旧版は一般的な`pinMode()`や`digitalWrite()`等を実装することより、Arduino IDEからEVT APIとEVT sampleを利用することを主目的としていました。新コアでいう「旧版互換」は、Arduino API互換よりEVT利用方法の互換が中心です。

調査時点で追跡対象は63ファイルで、そのうち33ファイルがEVT依存patchでした。EVT本体と生成後のArduino coreはGitで管理されていません。

主な生成フローは次の通りです。

1. WCHの配布URLから7系列のEVT archiveを取得
2. `EVT/EXAM/SRC`をfamily別coreへ全量コピー
3. GPIO Toggle exampleの`User`をcore skeletonとしてコピー
4. conf headerへ`debug.h`と全Peripheral headerを追加
5. overlayとpatchを適用
6. EVTの`User`ディレクトリをArduino examplesへ収集
7. ZIPを作成しpackage JSONのchecksumとsizeを更新

## 根本的な問題

### EVT全体とC++の結合

`Arduino.h`が`debug.h`をincludeし、加工されたconf headerが全Peripheral headerをincludeします。このため、利用していないvendor headerのC++不整合まで、すべてのArduino sketchのビルド障害になります。

この境界の広さが直接生んだのは、主にC++ linkageとheader構造のpatchです。startup constructorはCRT所有権、weak handlerはoverride設計、未使用引数や演算子修正はvendor code品質という別の問題です。

旧startup patchは`__libc_init_array()`を`SystemInit()`より前に呼ぶ構成でした。global constructorがclockやhardwareへ触れる場合に危険なため、新runtimeでは初期化順序をownし、実機で検証する必要があります。

### 再現性がない

- EVT URLにversionとSHA-256が固定されていない
- generatorが`php`、`wget`、`unzip`、`rename`、`patch`、`zip`等のhost環境へ依存する
- shell commandの終了statusやpatch rejectを厳密に検証しない
- temp stagingとatomic promoteがない
- vendor sourceの由来とライセンスinventoryを生成しない
- versionがscript、platform、library、package metadataへ分散している
- package JSON更新は先頭platformのchecksum/sizeが中心で、version、URL、archive名は別途手作業になる

### board/device modelが粗い

旧`boards.txt`にはCH32L103、V003、V006、V103、V20x、V307、X035の7系列がありますが、family名中心です。

- exact SKUとpackageを表現しない
- variant実体がない
- memory menuを変えても主に`upload.maximum_*`だけが変わる
- linker/startup選択が実際のmemory/device選択と連動しない
- V20x、V307のstartup wrapperが特定device variantへ固定されている

確認されたドリフト例です。

- package metadataからCH32V006が欠落
- packageではCH32V307をCH32V30xと表記
- CH32V003 GPIO exampleはGPIODを初期化した後にGPIOAをtoggle
- macOS x86_64用OpenOCD metadataがArm64 assetを参照する箇所がある
- `platform.txt`、release、libraryのversionが一致しない

### 古いtoolchainへ固定

- GCC 8系の`riscv-none-embed`を利用
- GNU++14と`-fpermissive`へ依存
- 新しいGCC定義がpackageに存在してもbuild recipeでは利用しない
- core archiveを`--whole-archive`でlinkする

### 書き込み対象を選べない

WCH-LinkE/OpenOCDのupload recipeに、probe serial、USB topology、fixture laneなどを指定する経路がありません。複数WCH-Link接続時に意図したDUTへ書き込む保証がありません。

## 継承する知見

コードをそのまま移植するのではなく、以下を設計知識または回帰試験として継承します。

- ArduinoからEVT形式の低レベルコードを利用する目的
- weak `main()`によりArduino `setup()/loop()`とEVT `main.c`を共存させる考え方
- Cコード向けの`c_main()`入口
- familyごとのISA/ABI、memory、runtime差分。ただし値は再検証する
- C++ constructorが必要であること
- weak ISR、C/C++ linkage、header破損、vendor実装バグに関する33 patchの知見
- GPIO、UART、global constructorを使ったfamily別smoke testの意図
- Arduino Board Managerからインストールしてbinを生成・書き込みする導線

## 新コアへ持ち込まないもの

- `EVT/EXAM/SRC`一式をcoreへ複製する構造
- 全vendor headerの暗黙公開
- EVT assemblyへの重複patch
- `-fpermissive`
- family名だけのboard定義
- 手書きの巨大な`boards.txt`
- patch失敗を許容するgenerator
- 無検証のEVT example全量収集
- probeの選択条件を渡せないupload recipe

## 移行上の注意

- 旧patchを新ソースへ機械的に適用しない。各patchが示すfailure modeをtestへ変換する
- 旧版とのバイナリ互換は目標にしない。ソース互換範囲と移行表を用意する
- legacy Board Manager indexや既存release assetを破壊的に置換しない
- 新コアはbeta indexまたは手動installationで十分に検証してからstable indexへ追加する

---

# 調査対象B: arduino_core_ch32_riscv_arduino 1.4.0

- Repository: [`ch32-riscv-ug/arduino_core_ch32_riscv_arduino`](https://github.com/ch32-riscv-ug/arduino_core_ch32_riscv_arduino)
- 調査対象: ローカルインストール済み `ch32-riscv-arduino:ch32riscv` 1.4.0(`~/.arduino15/packages/`)
- 調査日: 2026-08-19
- 関連: Q-013(内部HAL contract)、Q-019(コア拡張の置き場所)、Q-010(API取込)、Q-011(pin map)

このコアは`_noneos`と違い、**実際に`pinMode`/`digitalWrite`/`millis`/`analogRead`/`Wire`を実装しています**。したがって内部HAL contract(Q-013)の設計にあたっては、こちらが最も価値のある一次資料です。

## 構成

```text
cores/<FAMILY>/          CH32L103 CH32V003 CH32V006 CH32V103 CH32V20x CH32V307 CH32X035 の7つ
  Arduino.h              pin定義(PA0形式)、既定bus pin、ADC channel定義、HAL関数宣言
  port.c                 ★ family差の集約点
  main.c
  SRC/                   EVT全量(Core/Debug/Ld/Peripheral/Startup)
  USER/                  ch32xxx_conf.h / _it.c / system_ch32xxx.c
libraries/ArduinoCoreAPI/src/api/   ArduinoCore-API(bundle)
libraries/EVT/                      EVT互換ライブラリ
variants/                           **空**
```

`variants/`が空で、pin mapは`cores/<FAMILY>/Arduino.h`に手書きです(存在しないポートはコメントアウトで表現)。ここがADR-0005/Q-011の生成対象に置き換わる部分です。

## ★ 最大の発見: port.cが事実上の内部HAL contract

**7 familyすべてが、まったく同じ8関数を実装しています。** 経験的に安定した境界が既に出ているということです。

| # | 関数 | family差の性質 |
|---|---|---|
| 1 | `void ch32_board_init(void)` | NVIC PriorityGroup(**V307のみ`_2`**、他`_1`)、SysTick clock分周(**V103のみ`/8`**) |
| 2 | `void ch32_gpion_enable(uint8_t gpion)` | 存在するGPIOポート数とRCCバス(APB2等) |
| 3 | `void SysTick_Handler(void)` | SRクリア方法。**V103のみCNT 8バイトを個別ゼロ書き** |
| 4 | `void ch32_systick_init_config(uint64_t ticks)` | レジスタ幅/名前、NVIC優先度(V003/V006は設定なし、V20xは1、他15) |
| 5 | `unsigned long ch32_micros(void)` | SysTick CNT/CMPの読み方(V103のみポインタキャスト) |
| 6 | `uint8_t ch32_pin_to_adc(uint8_t pin)` | pin→ADC channelマップ(device固有) |
| 7 | `void ch32_adc_init(uint8_t adc_unit)` | ADC clock divider、インスタンス数 |
| 8 | `void ch32_i2c_init(uint8_t i2c)` | I2Cインスタンス数とRCCバス |

### SysTickは3世代に割れる

1. **V003 / V006**: `SR`/`CMP`/`CNT`/`CTLR`(32bit)、`CTLR=0xF`、NVIC優先度設定なし
2. **X035 / V20x / V307 / L103**: 同じレジスタ + `NVIC_SetPriority`(15。V20xのみ1)
3. **V103**: **完全に別物**。`CNTL0..3`/`CNTH0..3`/`CMPLR0..3`/`CMPHR0..3`の8bitバイト分割、64bit、`CTLR=(1<<0)`、さらにclockが`SystemCoreClock/8`

[ADR-0006](adr/0006-rtos-policy.ja.md)の「millis/delayのtickソースを差し替え可能にする」要件は、この実データで裏付けられます。V103を同じ抽象に載せられるかがcontract設計の検証条件になります。

### 新コアへそのまま持ち込めない点

- **EVT/SPL全面依存**: `#include <debug.h>`、`RCC_APB2PeriphClockCmd`、`ADC_InitTypeDef`、`NVIC_PriorityGroupConfig`、`USART_Printf_Init`。[ADR-0003](adr/0003-owned-startup-vector-linker.ja.md)でstartupはown化済みだが、port.cの中身はvendor API呼び出しそのもの。**契約の形は流用できるが実装はregister levelで書き直す**
- `ch32_board_init()`が`USART_Printf_Init(115200)`を**無条件で呼ぶ**。全sketchがEVT printfのコストを払う。[ADR-0004](adr/0004-runtime-and-cxx.ja.md)の「コアAPIはprintf非依存」と反する
- `platform.txt`が`-fpermissive`を使用([architecture](architecture.ja.md)が明示的に排除している)
- `ch32_micros()`が`millis() * 1000`を`unsigned int`で計算しており、**約71分でオーバーフロー**する

## pin番号の設計

`PA0`形式(上位3bit=ポート、下位5bit=ピン番号)で、Arduino慣例の連番`D0`/`D1`ではありません。

```c
#define CH32_GPIO_A (1 << 5)
#define PA0 (CH32_GPIO_A | (0))
```

多package・多familyを1つの番号体系で扱えるのが利点、`digitalWrite(13, HIGH)`のような標準的なsketchがそのまま動かないのが欠点です。新コアのpin番号設計(Q-011/Q-003)で引き継ぐかどうかを決める必要があります。

なお`CH32_UART1_TX PA10`と`CH32_I2C1_SCL PA10`が同一ピンに割り当てられており(X035)、既定bus pinの衝突は手書き管理では検出できていません。生成器で扱うべき理由の1つです。

## ★ ArduinoCore-APIの扱い(Q-010/Q-019の一次資料)

配置は`cores/`ではなく **`libraries/ArduinoCoreAPI/`**(ADR-0009で検討しなかった第3の選択肢)。

### 版が特定できない状態になっている

upstreamと突き合わせた結果:

- `ArduinoAPI.h`は`ARDUINO_API_VERSION 10501`(=1.5.1)と宣言
- しかし内容は**1.5.1ではない**。1.5.1以降に入ったlicense header追加が反映済みで、1.5.2で追加された`SPIBusMode`は未反映
- 実体は`1.5.1..1.5.2`間の**untagged master snapshot**(該当範囲16 commit中9 commitと一致。それ以上は特定不能)

**版マクロが実際の内容と一致していない**状態です。[ADR-0009](adr/0009-arduinocore-api-import.ja.md)がtag固定+tree hash記録+CI byte一致検証を求める理由の実例になります。

### `api/Print.h`にprintf patchを当てている

`Print.h.orig`(patch前バックアップ)が同梱されており、差分は`Print::vprintf` + `Print::printf`×2 overloadの追加(約50行)。ESP32系と同じ実装(64byteスタックバッファ、溢れたらmalloc)。

Q-019の案(a)「`api/Print.h`をpatch」を**前コアは実際に採用していた**ことになります。評価:

- 全メソッドがclass内inline定義なので、**呼ばれなければ何も出力されない**(odr-use されない)。未使用時コストはゼロ
- `Print&`越しの多態呼び出しが効く(案(b)の派生クラス方式にはない利点)
- 代償はupstream差分の恒久化。ADR-0009のbyte一致検証をpatch適用後treeとの比較へ組み替える必要がある
- 使用時コストは`vsnprintf`(newlib-nano `%d`で約4.9KB)+ `malloc`/`free` + 64byteスタック

## 書き込み

```text
tools.WCH_linkE.upload.pattern="{path}openocd" -f "{upload.config}" -c init -c halt \
  -c "program {...elf} verify; reset; wlink_reset_resume; exit;"
```

WCH OpenOCD一発で、**probe選択の口が一切ありません**。複数のWCH-Linkが接続されている場合に対象を指定する手段がない状態です。既存Arduinoコアがこの問題を解いていないことの裏付けになります([upload-and-fixture](upload-and-fixture.ja.md)、Q-041/Q-044)。

toolchainも`riscv-none-embed-gcc-8`(GCC 8.2系)で、[ADR-0002](adr/0002-toolchain-distribution.ja.md)のxPack 14.3.0-1とは別系統です。

## 新コアへの示唆

- **port.cの8関数はQ-013の出発点として使う。** 7 familyで安定していた実績があり、ゼロから設計するより確実。ただし実装はEVT非依存で書き直す
- V103のSysTickが別世代である事実を、tickソース抽象の設計要件に入れる
- `ch32_board_init()`から無条件のprintf初期化を外す
- pin番号体系(`PA0`形式 vs Arduino連番)は明示的に決める
- ArduinoCore-APIの版固定はtag+hashで行う(前コアの版不一致が実例)
- Q-019は「前コアがpatch方式を採用していた」を踏まえて判断する
