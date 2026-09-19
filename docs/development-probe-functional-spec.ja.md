# 汎用開発用プローブの機能仕様

文書状態: **リリース横展開に使う要求案**  
対象: ArduinoCore-CH32に限らない、複数MCUで実装可能な書込・試験プローブ

## 1. 目的

このプローブは単なる書込器ではなく、DUT（開発中のボード）に対する独立した相手役です。
同じ試験をV003以外のチップへ横展開できるよう、プローブのMCU名や内蔵周辺回路ではなく、
外部へ提供できる**機能と制約**を能力として宣言します。

プローブが担う役割は次の4つです。

1. firmwareの書込み、verify、reset、必要なら復旧
2. DUTとの制御・ログ用UART
3. GPIO、UART、I2C、SPIなどの独立したpeerおよび刺激源
4. 波形、電圧、電流、実行結果を同じrunへ記録する測定器

特定のプローブですべてを満たす必要はありません。小型MCU版は書込みとUARTだけ、ピン数や
測定機能の多い版はGPIO fixtureやlogic captureまで、といった構成を許します。テスト側は
能力宣言を読んで、実行可能な項目だけを選びます。未対応項目は失敗ではなく、理由付きのskipに
します。ただし、要求した能力の一部だけを黙って有効にすることは禁止します。

## 2. 機能クラス

機能は積み上げ式にします。製品名や使用MCUとは結び付けません。

| class | 必須機能 | 主な用途 |
|---|---|---|
| **P0 Writer** | 個体識別、書込み、verify、reset、排他、自己復旧 | firmware投入と生存確認 |
| **P1 Console** | P0 + 独立UART peer、timestamp付き送受信 | sketch結果の取得、serial試験 |
| **P2 Digital Fixture** | P1 + GPIO駆動/入力、pull、open-drain、UART/I2C/SPI peer | GPIOと一般的なserial busの機能試験 |
| **P3 Timing Fixture** | P2 + edge生成、logic capture、trigger、周波数/パルス計測 | PWM、tone、割込み、baud/clock精度 |
| **P4 Analog/Power Fixture** | P3 + 既知電圧、ADC、電源制御、電圧/電流測定 | ADC/DAC、brown-out、低消費電力 |
| **P5 Specialized Fixture** | USB/CAN/PD/Ethernet等の専用PHYまたはpeer | 高速・差動・電力を伴う周辺機能 |

P0/P1を最小構成とします。ただしDUTがUARTを1本しか持たない場合、制御UARTを使いながら
UART自体を検証できません。P2以上では、debug memory、別UART、専用agent線など、試験対象UARTを
空けられる制御経路があることを別能力として宣言します。

## 3. 能力宣言

### 3.1 宣言する情報

プローブは少なくとも次をmachine-readableに返します。

- probeの一意な個体ID、hardware revision、firmware version
- host transport、protocol version、最大同時session数
- 書込み可能なdebug/boot方式と、対応targetまたはalgorithm
- digital/analog channel数と各channelの電気的制約
- 各peerのcontroller/target対応、instance数、速度、mode、word長
- captureのchannel数、sample rate、buffer長、trigger、pre-trigger
- voltage domain、入力耐圧、drive電流、pull抵抗、open-drain可否
- 同時利用できない機能の排他groupと、連続pin等の配置制約
- 時間基準の精度、分解能、timestamp wrap、較正情報
- 電源出力、電圧/電流測定、保護回路の範囲
- attach/reset/writeがDUTのmemory、register、pinへ与える副作用
- 組込み済みdevice modelの名前、版、設定可能範囲
- firmware内で完結できるfault injectionの種類

GPIO番号をテストへ直接書きません。能力宣言では物理channelを列挙し、board fixture manifestが
`UART_TX`、`SDA`、`ADC_0`などの論理信号へ割り当てます。

### 3.2 宣言例

次はwire formatを決めるものではなく、必要な情報の例です。

```json
{
  "probe": {"serial": "probe-001", "hw": "A", "fw": "0.4.0"},
  "digital": {
    "channels": 16,
    "input_max_v": 3.6,
    "modes": ["input", "push_pull", "open_drain", "pull_up", "pull_down"],
    "max_toggle_hz": 8000000
  },
  "uart": [{"rx": true, "tx": true, "baud_max": 3000000,
             "parity": ["none", "even", "odd"], "faults": ["break"]}],
  "i2c": [{"controller": true, "target": true, "addresses": 2,
            "hz_max": 1000000, "faults": ["nack", "stretch", "stuck_sda"]}],
  "spi": [{"controller": true, "target": true, "modes": [0, 1, 2, 3],
            "bits": [8, 16], "hz_max": 12000000}],
  "capture": {"channels": 8, "sample_hz_max": 20000000,
                "samples_max": 131072, "pretrigger": true},
  "exclusive": [["spi.0", "capture.channels.4-7"]]
}
```

### 3.3 束縛と実行規則

1. hostはテストに必要な能力をまとめて要求します。
2. resolverはfixture配線と排他制約を考慮して物理channelへ束縛します。
3. 全要求を満たせない場合は、設定を一切変えず理由付きで拒否します。
4. 成立した束縛、実際の速度、電圧、時間精度をrun artifactへ保存します。
5. 結果にはpass/failだけでなく、実行した項目と能力不足でskipした項目を残します。

これにより、ESP32版で通った試験が別MCU版では静かに縮退することを防ぎます。

## 4. 共通基盤機能

### 4.1 書込みと復旧

必要な機能:

- erase/program/read/verify、差分書込み、明示した領域だけの操作
- target識別とimage/target不一致時のfail closed
- reset、run、halt、boot pin操作を個別に実行可能
- option/protectionを通常flashと別操作にし、破壊的操作は明示要求する
- 書込み前バックアップ、複数回読出し、多数決または再試行
- 電源断、通信断、probe watchdog後にhostから復帰可能
- targetへ触らないpassive attachと、attachの副作用一覧

検証できる範囲:

- uploader、flash内容、reset後の起動、bootloader/applicationの往復
- 不良image、verify不一致、途中切断からの再試行
- probeを替えた場合の書込み経路差

限界:

- debug/boot protocolとflash algorithmはtarget familyごとに必要
- read protection解除やmass eraseは元データを回復できない
- 書込み成功だけではclock、pin mux、application動作は証明できない

### 4.2 制御・ログ経路

UART consoleは最低限必要ですが、試験対象UARTと制御経路を分離できることが望まれます。

- binary透過、任意baud、RX/TX独立、timestamp、flush完了通知
- 行単位だけでなくbyte列として記録
- DUT reset中もhost側sessionを維持
- overflow、欠落byte、framing/parity/breakを明示
- debug memory/RTT/DMDATA等を利用できる場合はUARTと別能力として宣言

USB CDCの列挙やOS bufferを含むため、hostで受信できた時刻はDUTが送信した時刻そのものでは
ありません。厳密なbaud、latency、byte間隔はlogic captureで測ります。

## 5. ペリフェラル別の機能と到達範囲

### 5.1 GPIO / pinMode / digitalRead / digitalWrite

あると便利な機能:

- Hi-Z、push-pull HIGH/LOW、open-drain LOW/release
- pull-up、pull-down、可能なら両方同時
- driveとreadbackの同時実行、pin競合検出、直列保護
- 複数pinの原子的な更新とsnapshot
- edge count、pulse width、glitch生成

検証できること:

- pin mapping、方向、論理値、pull、open-drain、初期状態
- 複数port、EXTI、debounce、`pulseIn()`、`shiftIn/Out()`
- reset/boot中に意図しない出力が出ないこと

限界:

- 通常のdigital inputではthreshold、rise/fall time、drive電流を保証できない
- probeとDUTが同時にpush-pull駆動すると破損し得る
- 数nsのglitchや高速signal integrityはoscilloscope領域

### 5.2 外部割込み / timer input / pulse

あると便利な機能:

- hardware時刻に同期したsingle edge、pulse train、burst
- 周波数、duty、位相、jitterを設定可能
- stimulus edgeとDUT応答edgeを同一時間軸でcapture

検証できること:

- rising/falling/change interrupt、別port/bitのEXTI mapping
- interrupt latencyとjitter、取りこぼし上限
- timer input capture、counter、encoder入力の基本動作

限界:

- DUT内部の割込み禁止期間の原因までは外部波形だけでは分からない
- probeの生成jitterと測定jitterより小さい差は評価できない

### 5.3 PWM / tone / output compare / Servo

あると便利な機能:

- digital capture、周期/duty/high/low時間の集計
- 複数channel同時captureと位相比較
- 長時間edge count、missing pulse検出

検証できること:

- `analogWrite()`の周波数、duty、0%/100%、pin route
- `tone()`の周波数、duration、停止状態
- Servo pulse幅、周期、複数channel共有
- TIM takeover後に旧ownerが停止し、lease喪失を検出すること

限界:

- capture sample rateの数分の1より速いPWMは正確に測れない
- GPIO波形だけではtimer registerの所有者や内部clock sourceは特定できない
- motor等の負荷を接続した電力段の動作は別治具が必要

### 5.4 ADC

あると便利な機能:

- 0V、VDD/2付近、VDD付近を含む既知電圧出力
- 可能なら較正済みDAC、最低構成ではpull/DAC/PWM+RC
- probe側ADCまたはDMMによる実電圧readback
- source impedance、settling time、noiseの宣言

検証できること:

- channel/pin mapping、単調性、端点、概略gain
- resolution切替、連続channel切替、sample time依存
- 内部Vref/温度channelとの相対比較

限界:

- pull-up/downの中点は抵抗ばらつきが大きく、精度・INL/DNL試験には使えない
- 3点だけではADCの全code、linearity、noise性能を保証できない
- DUTのVDDを基準にするADCでは、刺激電圧と同時にVDDを測る必要がある

### 5.5 DAC / comparator / OPA

あると便利な機能:

- probe側ADCによる電圧測定
- programmable analog stimulus、threshold sweep
- high impedance入力と既知負荷の切替

検証できること:

- DAC端点、単調性、概略settling
- comparator threshold/hysteresis、internal routingの外部結果
- OPAの基本gainとsaturation

限界:

- probe ADCの精度以上は評価できない
- bandwidth、THD、offset等の精密analog特性は専用計測器が必要

### 5.6 UART

あると便利な機能:

- peerとして任意baud/data bits/parity/stop bits
- full duplex、RTS/CTS、break
- framing/parity error、byte欠落、間隔、連続streamの注入
- logic captureによるbit幅測定

検証できること:

- TX/RX、buffer、timeout、複数instance、baud誤差
- parity/stop設定、overflow、break、flow control
- reset直前直後や長いburstでの欠落

限界:

- consoleと試験対象を同じUARTにすると、そのUARTを完全には検証できない
- USB-UART bridge越しのhost時刻ではbit timingを測れない
- 電圧規格が異なるRS-232/RS-485にはtransceiverが必要

### 5.7 I2C / SMBus

あると便利な機能:

- controllerとtargetの両役割
- 複数address、7/10 bit、register file、block model
- open-drain、外付けpull-up値とbus電圧の宣言
- NACK、clock stretch、SDA/SCL stuck、arbitration、glitch注入
- transaction decodeとraw edge capture

検証できること:

- DUT controller/target、repeated START、scan、read/write、複数instance
- NACK/timeout/bus recovery、stretch、buffer境界
- EEPROMやsensor等のdevice modelを使ったlibrary試験

限界:

- push-pull GPIOによる疑似I2Cではelectrical contractを検証できない
- probe firmwareがhostへ問い合わせてから応答する方式は期限に間に合わない
- 高速modeのrise time、capacitance、level shifter挙動は実配線条件に依存する

### 5.8 SPI / QSPI

あると便利な機能:

- controllerとtarget、mode 0～3、MSB/LSB、可変word長
- CSごとの複数model、full duplex preload/readback
- busy pattern、MISO Hi-Z/保持、応答遅延、途中CS解除
- SCK/MOSI/MISO/CSの同時capture

検証できること:

- mode、bit order、clock上限、CS timing、buffer境界
- 複数SPI instance、transaction設定切替
- flash、SD、display等の上位libraryの基本transaction

限界:

- target応答はfirmware内で完結する必要があり、host round-tripは使えない
- probeのtarget hardwareが対応しないword長やCS挙動はGPIO/programmable I/O実装が必要
- QSPI/OctoSPIや高速signal integrityはpin数、DMA、配線品質が別の制約になる

### 5.9 1-Wire / WS2812 / IR / software peripheral

必要機能はprotocol固有peerより、programmableなedge sequencerとcaptureを基本にします。

検証できること:

- software UART/SPI/I2C、1-Wire、IR、addressable LEDの波形とdecode
- libraryが許容するtiming範囲と、割込みによるjitter

限界:

- protocol decoderが無い場合は波形一致までで、意味的な正しさは判定できない
- CPU polling型probeでは高速または厳密な波形を生成できない場合がある

### 5.10 CAN / LIN / RS-485

必要機能:

- 対応PHY、終端、controller/peer、frame timestamp
- ACK/error frame、bit error、bus-off、dominant保持の注入

検証できること:

- frame送受信、filter、再送、error recovery、複数nodeの基本動作

限界:

- GPIO直結では物理層を検証できない
- CAN arbitrationには最低2node相当が必要
- EMC、長配線、termination marginは専用設備の範囲

### 5.11 USB device/host

必要機能:

- USB hostまたはdevice peer、VBUS switch/測定、enumeration log
- control/bulk/interrupt/isochronousの対応範囲宣言
- detach、reset、stall、short packet等の注入

検証できること:

- descriptor、enumeration、class基本転送、再接続

限界:

- FS/HSは別能力。FS対応probeでHS throughputは検証できない
- eye pattern、impedance、complianceはUSB専用測定器が必要
- host OSを相手にした試験も残す。probe peerだけではdriver互換を保証できない

### 5.12 USB PD / 電力protocol

必要機能:

- source/sink peer、CC PHY、VBUSの絶縁された測定と遮断
- profile/PPS、role swap、fault、過電圧interlock

検証できること:

- negotiation、request、keepalive、拒否・timeout処理

限界:

- 誤設定がDUTを破壊し得るため、softwareだけの保護を認めない
- 高電力・規格complianceは認証設備が必要

### 5.13 I2S / audio / parallel / camera / display

必要機能:

- 多channel同期capture/generation、DMA、外部clock、十分なbuffer
- frame/line clockとdataの関連を保持したartifact

検証できること:

- protocol framing、clock比、短いframe、簡単なframebuffer/audio pattern

限界:

- pin数と帯域がprobe classを強く制限する
- 連続stream、画質、音質、signal integrityは短いburst試験だけでは保証できない

### 5.14 Ethernet

必要機能:

- 対応PHY/MAC、link状態、packet generator/capture、timestamp

検証できること:

- link、基本packet、MAC/filter、短時間throughput

限界:

- RMII/MIIをGPIO fixtureだけで代替するのは現実的でない
- 長時間負荷、TCP/IP stack、規格適合は別試験を残す

### 5.15 RTC / watchdog / sleep / low power

あると便利な機能:

- 電源制御、reset reason取得、wake pin/RTC edge生成
- µA～mAを覆う電流測定、長時間timestamp
- DUT UARTが停止しても維持される制御経路

検証できること:

- watchdog reset、wake source、sleep復帰、概略消費電流

限界:

- probeから給電するとground leakageやdebug線のback-powerが測定を汚す
- 長期RTC精度や温度特性は短時間HILでは確認できない

## 6. Logic captureと測定結果

captureは「見えた」ではなく、自動判定と再解析に使える必要があります。

- raw sampleを保存し、sample rate、trigger、channel mapを同梱
- dropped sample数を必ず報告し、欠落時はgoldenへ採用しない
- burst、continuous、pre-triggerを能力別に宣言
- probeが駆動した線も同時にreadbackする
- protocol decode結果だけでなくraw波形を残す
- host、probe、DUTのeventを共通run IDへ集約
- probe firmwareとdecoder versionを保存

短いUART frame、I2C transaction、PWM数周期はburst captureで十分です。高速・長時間streamを
すべて無圧縮で保存できることを最小要件にはしません。

## 7. 電気的安全性

汎用プローブではpin機能より先に電気条件を確定します。

- DUT VIOを測定し、対応範囲外ならdriveを禁止
- channelごとの直列抵抗、overvoltage/overcurrent検出
- push-pull同士の競合を設定時とreadback時に検出
- open-drain busはpull-up電圧と抵抗をmanifestへ記録
- VDD/VBUS供給元を1つに限定し、back-powerを防ぐ
- reset、boot、debug線は通常Hi-Z。明示した操作中だけ駆動
- power/PD試験はhardware interlockを持つ
- fixture self-testでshort、stuck、誤配線をDUT接続前に検査

## 8. Device model

I2C sensor、SPI flash、SD、display等は、protocol engineとdevice modelを分離します。

- 転送期限内の応答はprobe内で完結
- hostは試験前にregister/dataをpreloadし、試験後にreadback
- model名、version、設定値を能力とartifactへ記録
- register file、block storage、byte stream等の汎用modelを優先
- 実時間を短縮したmodelはその事実を宣言し、timing試験には使用しない
- 実物deviceをfixtureへ搭載する場合も、modelと同じ能力語彙で宣言

## 9. 横展開時の試験レベル

各target boardは、配線と利用可能なprobe classに応じて次を宣言します。

| level | リリース前に要求する証拠 |
|---|---|
| **L0 Build** | 全FQBN/exampleのcompile、size、静的pin/peripheral表 |
| **L1 Boot** | 書込み、verify、reset、UART heartbeat |
| **L2 Core GPIO** | header GPIO、pull、外部割込み、ADC 3点 |
| **L3 Serial Bus** | UART、I2C、SPIを独立peer相手に双方向確認 |
| **L4 Timing** | PWM、tone、timer、baud/clock、interrupt latencyの波形確認 |
| **L5 Power/Special** | sleep/current、USB/PD/CAN/Ethernet等、搭載機能別 |

すべてのチップへ同じ最大levelを要求するのではなく、**そのチップでコアが提供すると宣言する機能**に
対応したlevelを要求します。例えばADCを持たないtargetにADC試験は不要ですが、UARTを提供するのに
UART peer試験がskipされた場合はリリース証拠が不足しています。

## 10. 最小構成と推奨構成

最小構成:

- 安定した個体識別
- 安全な書込み/verify/reset
- 1本のbinary-transparent UART
- 2～4本のGPIO drive/read
- capability、排他、エラー理由のmachine-readable出力
- watchdogとhostからの完全reset

横展開用の推奨構成:

- UART peer 2、I2C target 2 address、SPI target 1
- 8本以上の任意GPIOとopen-drain/pull
- 8channel以上のburst capture、pre-trigger
- edge/pulse/frequency generator
- 既知電圧1出力、電圧readback、DUT power cycle
- 制御経路と試験対象UARTの分離
- fixture self-test、raw artifact、replay

pin数が多いprobeは同時接続範囲が広がりますが、試験の意味は同じです。小型probeを不完全版とせず、
能力宣言に基づいて同じhost testを再利用できることを最優先にします。

## 11. 無印ESP32による暫定プローブの実証

2026-09-19時点で、無印ESP32（ESP32-D0WD-V3）を暫定プローブとしてUIAPduino Pro Micro
CH32V003 V1.4を試験した。この構成は本書の全機能を備えた完成プローブではないが、P0～P2の
縦方向の一部を実機で結び、機能を分離して組み合わせられることを確認した参照fixtureである。

### 11.1 実際に採用した経路

UIAPduinoは、製品の標準書込み経路がsoftware USB HID bootloader `1209:b803`であるという
特殊事例である。そのためrelease HILでは、書込み自体は製品HID経路を使い、ESP32は次を担当した。

| 機能 | 採用した経路 | ESP32の役割 |
|---|---|---|
| application書込み・verify | host → 製品software USB HID → V003 | boot modeへの復帰を補助。通常の書込みdataは中継しない |
| boot/application切替 | ESP32 GPIO16 → PD1/SWIO | BOOT_MODE設定用RAM payloadの注入、CPU resume、software reset、状態確認 |
| reset | SWIOからV003自身にsoftware resetを実行させる | 外部RESET線は通常不使用。GPIO23はHi-Z |
| console | ESP32 UART2 ↔ DUT UART | commandと結果の転送、pin mux切替後の再同期 |
| digital/analog fixture | ESP32 GPIO/DAC/pull ↔ DUT header | GPIO drive/read、ADC 3水準の刺激 |
| serial bus peer | ESP32 I2C target / SPI target ↔ DUT | DUTのI2C/SPI controllerを独立peerとして検証 |

実装と実行手順は
[`tests/manual/uiapduino_fixture/`](../tests/manual/uiapduino_fixture/README.ja.md)、HIDとSWIOによる
復旧手順は[`uiapduino-hid-upload`](uiapduino-hid-upload.ja.md)にある。13,780 byteの製品用imageを
HIDから書き込み、UART、header 12信号、ADC 6入力、I2C、SPIを1 suiteで確認した。

別のend-to-end実験では、同じGPIO16→PD1/SWIOだけでArduino image 5,220 byteをuser flashへ
直接書き込み、82個の64 byte pageをread-back verifyし、`setup()`実行と製品bootloaderへの復帰まで
成立した。したがってESP32がP0 Writerを直接担うことも技術的には可能である。ただしUIAPduinoの
release経路では、製品が通常利用するHID uploader自体も検証するため、あえてHID書込みを採用した。

### 11.2 一般化するときの原則

UIAPduinoの構成を、すべてのtargetの標準形にはしない。

- 製品に標準bootloader経路があり、それ自体が検証対象なら、その経路から書き込む。
- 標準bootloaderが無い、復旧できない、またはdebug書込みが通常経路なら、SWIO/RVSWD等を
  プローブが直接操作してerase/program/verifyする方が単純である。
- 「書込みdata path」と「boot/reset control」と「周辺機能fixture」は別能力として宣言する。
  1台のプローブがすべて担当しても、別経路を組み合わせてもよい。
- 書込みに使わないdebug線も、状態確認、halt、memory read、boot復旧に使用できる。その副作用と
  操作した領域をrun artifactへ残す。
- target固有のboot条件（UIAPduinoのPD4等）は汎用SWIO能力へ混ぜず、board fixture manifestまたは
  board固有procedureとして分離する。

### 11.3 実証から追加された実装要件

- SWIO/DMIでは成功statusだけを信用せず、address/valueまたは書込先をread-backする。実験中に
  一時的な1 bit誤りを実際に検出した。
- RAMへ注入する実行payloadは全wordを照合してからDPCを設定し、resumeする。
- flashはpage単位のerase/program/read-back verifyを再実行可能にし、上限付きで再試行する。
- host→probeのbinary要求にはCRC等の完全性検査を持たせ、破損時はerase前に拒否する。
- USBIP等の接続管理状態と物理的なUSB列挙を同一視しない。消失したdeviceが管理上`Attached`のまま
  残る場合があるため、列挙、target状態、実行結果の複数証拠で判定する。
- fixtureがDUTのUART兼用pinをADC等へ切り替える場合、command受信不能期間とUART再同期を
  test sequence自身が扱う。

### 11.4 OEP試作へ切り出せる範囲

この実証をそのまま完成版OEPプローブと見なす段階ではないが、**破棄可能な最小縦断試作**を始める
材料は揃った。目的はwire formatを固定することではなく、異なるhost/probe実装で同じ能力を発見・
実行・再現できるかを確かめることである。

最初の試作範囲は次に限定する。

1. probe/firmware/protocol versionと個体IDの取得
2. SWIO boot control、target状態取得、必要ならdirect flashを独立serviceとして列挙
3. UART、GPIO、ADC stimulus、I2C target、SPI targetの能力と排他groupの列挙
4. fixture manifestに基づく論理信号から物理channelへの束縛
5. accepted/rejected/started/completed/failedの区別と、実際に採用した条件・再試行・artifactの取得
6. HIDのようなOEP外data pathを使う場合、そのbindingと役割を明示し、OEP serviceの成功と
   外部転送の成功を混同しないこと

最初からP0～P5全機能、streaming capture、全target共通flash algorithm、最終transport、最終的な
binary encodingを固定しない。現在のASCII commandやPython orchestrationは参照fixtureとして残し、
OEP試作は同じ実機結果を別host実装から再現できるかを比較する。これなら試作が仕様を先回りせず、
不足している上流要件を実測で見つける用途になる。

## 12. この仕様だけでは決めないもの

- probeに使うMCUやprogrammable I/Oの種類
- USB class、具体的なwire protocol、CLI syntax
- connector形状と固定pin assignment
- すべてのdevice modelをfirmwareへ同梱するか
- continuous logic analyzerの最大性能

これらは実装候補ごとに異なります。本書は、それらを選ぶ前に比較可能にする機能境界を定めます。

詳細要求ID、排他、artifact、既存repositoryとの接続は
[harness-requirements](harness-requirements.ja.md)、評価根拠は
[harness-probe](harness-probe.ja.md)を参照してください。
