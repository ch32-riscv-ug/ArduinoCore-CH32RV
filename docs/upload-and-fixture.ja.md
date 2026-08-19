# 書き込みと実機fixture

文書状態: 提案および要実機検証

書き込み経路×familyの対応表、互換programmerのエコシステム、upload_methodメニュー構成案は[R-17調査](research/upload-programmers.ja.md)を参照。

## 問題設定

旧コアはWCH-LinkEとWCH OpenOCDを直接呼び出しますが、複数probeから特定の1台を安全に選ぶ仕組みがありません。

書き込みprotocolとArduino integrationを直接結合すると、programmer変更のたびに`platform.txt`、CI、fixtureを変更する必要があります。新コアでは安定したフロントエンドを設けます。

既存toolまたはそのupstream改善で、書き込み先の一意な選択、machine-readableな結果、verify、resetを実現できない場合は、新しいfrontend、backend、programmer firmwareの開発も選択肢にします。新規toolはArduino専用にせず、ELF/BIN等の一般的な入力と安定したCLI/APIを持ち、`ch32fun`等のCH32環境からも利用できる独立componentを目標にします。

## Upload frontend案

仮称を`ch32-upload`とします。正式名称は未決定です。

```text
ch32-upload list --json
ch32-upload info --probe <id> --json
ch32-upload flash --probe <id> --target <device> --elf <path>
ch32-upload verify --probe <id> --elf <path>
ch32-upload reset --probe <id>
ch32-upload read-uid --probe <id> --json
```

必要な性質:

- machine-readable output
- 安定したexit code
- backend固有logをartifactとして保存
- probe型番/HW revision/mode/FW version、backend version、target、DUT firmware SHA-256を分けて記録
- targetが曖昧ならfail-closed
- 複数の同型programmerを列挙し、呼出側が指定した1台だけを確実にopenできる
- flash後のverifyを標準経路にする
- device保護状態と復旧手順を区別する

## Backend候補

| 優先度案 | Backend | 位置付け |
|---|---|---|
| 1 | `probe-rs` | 第一候補。全対象SKUとWCH-Link FWで認定する。**0.32時点でV407/X315/M030/V205のtargetが無い**(R-17)ため、gap分は下位backendまたはupstream貢献で埋める |
| 2 | `minichlink` / `wlink` | 代替実装、protocol調査、fallback候補 |
| 3 | `wchisp` | USB/UART ISPを持つ対象だけのfallback |
| 4 | WCH OpenOCD | legacy互換。source/build/licenseを確認 |

frontendはbackendのCLI textを公開contractにせず、内部adapterで吸収します。

WCH-Link host protocolはfirmware依存の解析部分があるため、probe firmwareを暗黙に更新しません。認定済みfirmware matrixと更新・rollback手順をfixture inventoryへ記録します。

## 複数WCH-Linkの識別

**maintainer報告(2026-08-19)**: WCH-LinkEは複数台所有しているが、**同時に接続して使うことができず、実際には1台しか接続できない**。

これはfixture設計だけの話ではなく、**独自書き込みツール開発([R-17](research/upload-programmers.ja.md)の`ch32-upload`、Q-044/Q-045/Q-049)の昇格条件に直接効く報告**です。既存文書は次の条件を書いていました。

- R-17: 「書き込みソフトの自作(`ch32-upload` frontend)は既定方針どおり。backend gapと**probe選択問題(Q-041)が既存toolで埋まらない場合に本体開発へ昇格**」
- Q-045: 「**Q-041を既存toolで解決できなければ優先度を上げる**」

したがってこの報告は、`ch32-upload`および独自programmer(Q-045)の**優先度を上げる方向の証拠**として扱います。確定には原因切り分けが要ります。

### 既存実装の裏付け

旧コア`arduino_core_ch32_riscv_arduino` 1.4.0のupload recipeは`openocd -f wch-riscv.cfg -c init -c halt -c "program ...; verify; reset; wlink_reset_resume; exit;"`で、**probe選択の引数が一切ありません**([legacy-audit](legacy-audit.ja.md)調査対象B)。openwch公式coreも同様にOpenOCD一発です。既存のCH32 Arduinoコアはこの問題を解いていません。

### 切り分けに使える実測(2026-08-19、開発機のWCH製USBデバイス)

| device | VID:PID | 台数 | USB serial descriptor |
|---|---|---|---|
| CH343 (USB Single Serial) | `1a86:55d3` | 4 | **個体ごとにunique**(`58FA041019`/`5B5E058417`/`5B5F091220`/`5B5E057951`) |
| CH340 | `1a86:7523` | 3 | **なし**(topology pathでしか区別できない) |

**WCHは製品によってserialの有無が違う**ことが実測で確認できました。CH343 4台とCH340 3台の計7台が同時接続できています。したがってWCH-LinkEが繋げない理由は「WCH製だから」ではなく、LinkE固有の事情です。

**次の実験(所要1分)**: WCH-LinkEを2台挿して以下を実行し、`serial`欄を確認する。

```sh
for d in /sys/bus/usb/devices/*/; do
  v=$(cat $d/idVendor 2>/dev/null); [ "$v" = "1a86" ] || continue
  echo "$(basename $d) $(cat $d/idProduct) devpath=$(cat $d/devpath) serial=$(cat $d/serial 2>/dev/null)"
done
```

結果による分岐:

| 観測 | 意味 | 対応 |
|---|---|---|
| serialがunique | tool側がserial selectorを使っていないだけ | probe-rs/wlinkの`--probe VID:PID:SERIAL`で解決。独自tool不要 |
| serialが全台同一 | USB descriptorレベルで区別不能 | topology path選択が必須 → 既存toolに無ければ`ch32-upload`本体開発へ昇格(Q-044) |
| 2台目が列挙されない/掴めない | driver/firmware側の制限 | Q-048(FW固定方針)と合わせて再検討。mux構成が現実解 |

### 当面の扱い

- 単一DUTで進む限りこの制約は開発をblockしない
- 「運用優先構成」(1 DUT laneにつき1 WCH-LinkE)は複数LinkEの同時接続を前提にしているため、**reference fixtureの第一候補から外す**
- 多DUT化は「台数削減構成」(1 LinkE + debug signal mux)を第一候補として設計する
- Q-043は共有+mux側へ大きく傾く

USB serialと列挙indexは恒久IDとして扱いません。

識別の優先順位案:

1. 実際にuniqueで安定していることを確認したhardware serial
2. 固定USB hubの物理port/topology
3. fixture controllerが管理するlane
4. 選択後に読み出したtarget UID/chip ID

`--device 0`や「最初に見つかったprobe」は使用しません。複数候補が残る場合は書き込みを中止します。

USB hubのPer-Port Power Switching（PPPS）で対象以外をpower offまたはdisconnectし、「見えている1台」を書き込み先とみなす方式は採用しません。PPPSはDUTのpower cycleや故障復旧には利用できますが、programmerまたはDUTのidentityと選択を代替しません。

Arduino IDE/CLIには[Pluggable Discovery](https://docs.arduino.cc/arduino-cli/pluggable-discovery-specification/)を使い、例えば次のようなaddressを公開できます。

```text
wchlink://bench-01/lane-1
wchlink://bench-01/lane-2
```

Pluggable Discoveryが物理laneを表示できることと、backendがそのUSB deviceだけをopenできることは別問題です。調査時点の一般的なselectorはVID/PID/serialまたは列挙index中心です。次のいずれかで物理laneからopen対象までを一意に解決できることを、最初のHIL runner OSで検証します。

- backendまたはupstreamへUSB topology selectorを追加する
- laneごとにUSB deviceをhost/container/namespaceへ隔離する
- 共有LinkEをfixture controllerのmuxで唯一のDUTへ接続する

upload前に物理laneと期待deviceを照合します。UIDはSKU、backend、保護状態によって`required`、`optional`、`unavailable`をmanifestへ持たせます。UIDを読める場合は台帳と照合し、読めない場合は物理lane、chip ID、起動後のboard/build IDを組み合わせます。

## Fixture構成案

### 決定済みの初期構成

最初のvertical sliceでは、次の1組を使用します。

- WCH-LinkE 1台
- FX2LP系logic analyzer 1台。まず8 channel、8 MHzで運用する
- DUT 1台
- Arduino `Serial`の送受信にはWCH-LinkE内蔵の物理UARTを使用する
- DUTのUART TX/RXはWCH-LinkEへ接続し、必要な信号をlogic analyzerから並行して観測する
- MCU/boardはadapterまたは配線を交換して逐次試験する

WCH-LinkEのSDI virtual serialは通常のArduino `HardwareSerial`認定には使用せず、bring-upや障害解析の補助候補とします。logic analyzerの各channelをどのDUT pinと論理信号へ接続するか、adapter connector、電源供給・制御方法は未決定です。

### CH32X035を初期対象にする場合のピン制約(2026-08-19調査)

`ch32-device-data`から確認した、**配線前に確定させる必要がある**制約です。

**固定で占有されるpad(全X035パッケージ共通)**

| pad | 機能 | 影響 |
|---|---|---|
| `PC18` / `PC19` | `DIO` / `DCK`(SWD) | WCH-LinkE接続に必須。他用途に使えない |
| `PC16` / `PC17` | `UDM` / `UDP`(USB D-/D+) | USB・USB-PDに必須 |
| `PC14` / `PC15` | `CC1` / `CC2`(Type-C CC) | USB-PDに必須 |

**★ I2Cがこれらと衝突する**

X035のI2C1は6 routeあるが、行き先が限られる。

| route | SCL | SDA | 衝突 |
|---|---|---|---|
| default | PA10 | PA11 | なし |
| remap-1 | PA13 | PA14 | なし |
| remap-2 / 4 | PC16 / PC17 | PC17 / PC16 | **USB D-/D+** |
| remap-3 / 5 | PC19 / PC18 | PC18 / PC19 | **SWD** |

`PA10/PA11`と`PA13/PA14`がpadとして出ているのは**CH32X035C8T6(LQFP48)とCH32X035R8T6(LQFP64)だけ**。`G8R6`はSCL(PA13)のみでSDAが無く使えない。

したがって**小パッケージ(F8U6 / G8U6 / F7P6 / D8U6)では、SWD・USB・I2Cのどれか1つを必ず諦める**ことになる。v1.0がI2CをTier Aに含み、USB-PDの優先度も高い以上、**初期fixtureのDUTはCH32X035C8T6(LQFP48)以上**が要件となる。

**エラッタによる追加制約**([device-data errata](device-data.ja.md))

- `x035-adc-ch-i2c-unavailable`(ロット番号の下から5桁目=0): **ADC ch3/7/11/15とI2Cが使えない**
  - fixtureのADC試験には**ch3/7/11/15以外**(A0/A1等)を割り当て、影響ロットでもADC試験が成立するようにする
  - I2C試験には非該当ロットの個体が必要。fixture inventoryにロット番号を記録する
- `x035-pc10-pc17-bonded`(F8U6/D8U6以外): PC10/PC17とPC11/PC16が内部結線。**PC10/PC11はどのパッケージでもpadとして出ていない**ため配線の問題ではないが、**コアがPC10/PC11をoutputに設定してはならない**。variant生成でunusableとして表現する(Q-011)

### Logic analyzerのchannel数(先に決める必要がある項目)

v1.0の完成条件(GPIO/UART/SPI/I2C/ADC/PWM/割込みがTier A)を波形で検証するには、次の論理信号が要る。

| 論理名 | 用途 |
|---|---|
| `MARKER` | 測定開始点。RUN受信後にDUTがtoggle |
| `GPIO_OUT` | digitalWrite |
| `INT_IN` | attachInterrupt(hostが駆動) |
| `PWM` | analogWrite |
| `UART_TX` / `UART_RX` | HardwareSerial |
| `I2C_SCL` / `I2C_SDA` | Wire |
| `SPI_SCK` / `SPI_MOSI` / `SPI_MISO` / `SPI_CS` | SPI |

**合計12**(clock検証用の`MCO`を足すと13)。当初案の**8 channelでは足りず**、SPI群とI2C群を同時に張れないためtest group単位で配線し直しになる。

→ **16 channelを推奨**。これはfixture部材とconnector設計に直結し、後から変更するコストが最も高い項目。

sample rateはFX2LP系がUSB 2.0帯域律速のため、**8ch@24MHz と 16ch@12MHz** のトレードオフになる。Arduinoのconformance対象としてSPIを1MHz級で回すなら12MHzで12倍oversamplingとなり足りる。8MHz級SPIの波形判定が必要になった時点で別機材を検討する。

### 変更コストによる3層の切り分け

「後から変えるのが大変」という観点では、決める順序が分かれる。

| 層 | 内容 | 変更コスト | いつ決めるか |
|---|---|---|---|
| 論理信号名 | `MARKER`/`GPIO_OUT`/… | 低。[test-strategy](test-strategy.ja.md)が論理名のみをtestへ書くことを既に義務化 | **今** |
| LA channel数・connector | 16ch、ピンヘッダ配列、電源/GND位置 | **高**(部材購入・治具再製作) | **今** |
| 物理pin割当 | `PA5=SPI_SCK`等 | 中。board metadataとして差し替え可能 | 対象boardの確定後 |

### 運用優先構成

初期費用を許容し、運用を単純にする構成です。

- 1 DUT laneにつき1 WCH-LinkE
- managed USB hubの固定port
- laneごとのDUT電源制御
- 固定UART adapterまたはLinkE UART
- logic analyzer channelを論理信号へmapping
- fixture controllerによるreset、boot、power cycle

USB serialが重複していても、物理portとlaneを論理的には固定できます。各backendがそのpathのprobeを確実に選択できる方法を実装・検証できた場合に、並列試験へ拡張できます。

### 台数削減構成

- 1 WCH-LinkEをdebug signal mux/relayで複数DUTへ切り替える
- 選択lane以外をhigh impedanceまたはpower offにする
- fixture controllerがmux、power、resetを制御する
- flashは直列実行する

部品費は減りますが、信号品質とfixture固有故障が増えるため、reference fixtureには運用優先構成を推奨します。

## ESP32-S3/RP2040等の役割

第一段階ではESP32-S3やRP2040を次のfixture controllerとして利用できます。

- unique fixture IDの提供
- power/reset/mux制御
- voltage/currentなどfixture healthの測定
- hostとの制御protocol
- `digitalRead`、external interrupt、ADC向けの信号／電圧生成
- SPI/I2C/UARTのbus peer、loopback、NACK等のerror injection
- 必要に応じたlevel shiftingと信号隔離

CH32 programmer自体の置換は別の研究項目にします。ESP32系またはRP2040を使うprogrammerは、固定したunique ID、複数台の決定的な選択、open firmware、cross-platformなhost protocolを一体で設計できる可能性があります。既存backendで選択問題を解決できない場合には優先度を上げます。

開発する場合もArduinoCore-CH32だけの書き込みrecipeには閉じず、`ch32fun`等から同じCLI/protocolを利用できることを要件にします。初期releaseを独自programmerの完成へ無条件に依存させるかは、既存toolの評価後に判断します。

## Fixture manifest案

```yaml
id: bench-01
dut:
  board: <board-id>
  expected_uid: null
  uid_policy: optional
  control_serial: /dev/ch32/bench-01-uart
programmer:
  backend: probe-rs
  usb_path: <stable-physical-path>
  probe_model: WCH-LinkE
  probe_hw_revision: <revision>
  probe_mode: RV
  probe_firmware: <version>
logic_analyzer:
  driver: fx2lafw
  connection: <stable-connection>
  sample_rate_hz: 8000000
  channels:
    # channel mappingはboard/adapter選定後に決定する
    marker: <channel>
    gpio_out: <channel>
    uart_tx: <channel>
power:
  backend: <controller>
  lane: 1
```

実際のOS依存pathはlocal inventoryに置き、公開repositoryへ個人環境の値をcommitしません。

## Host側driverとpermission

- Linuxのudev rule、group、USB permissionはfixture imageのprovisioningで管理する
- Windowsのdriver/backend bindingを認定項目に含める
- macOSでは署名・quarantineを含む配布CLIの起動を確認する
- Board Manager installから`sudo`やsystem-wide driver/rule変更を暗黙に実行しない
- 必要なhost設定はpreflightで検出し、明示的なsetup手順を案内する

## Preflight

HIL開始前の確認を二段階に分けます。

### Fixture health

- fixture lockを取得できる
- programmerがちょうど1台に解決される
- power/reset制御が応答する
- Serialが唯一に解決される
- logic analyzerと必要channelが存在する
- programmer、analyzer、stimulus controllerをDUTに依存しない範囲でself-testする
- 必要に応じて既知good診断firmwareでfixture全体を定期検証する

### Candidate core test

- candidate firmwareをflash/verifyできる
- expected device/chip IDと一致する
- target UIDが利用可能ならpolicyに従って台帳と一致する
- reset後にcandidate firmwareの`READY`へ到達する
- build/board IDが期待値と一致する

fixture healthの失敗は下流testをskipまたは停止します。一方、candidateを書いた後のflash、boot、`READY`、Serial失敗は原則としてcore test failureです。これをfixture failureへ分類して回帰を隠しません。
