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
