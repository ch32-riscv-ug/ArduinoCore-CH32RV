# TODO

決まったことは ADR や各文書に移し、ここからは消します。

全体の作業順、成果物、判断条件とモデル間の引き継ぎは
[コア全体再設計計画](CORE_REDESIGN_PLAN.ja.md) を正本とします。
この TODO は個別の検討事項と予備調査を保持します。

クラス・主要動作モードの追加候補と、現行で対象外の機能の採否検討は
[追加機能の採否検討](FEATURE_CANDIDATES.ja.md) にまとめます。


## テスト計画

2026-10-24 の検討は [tests/test-plan-review-2026-10-24.ja.md](tests/test-plan-review-2026-10-24.ja.md) に移した。引き継いだ案は
[tests/TEST_DESIGN.ja.md](tests/TEST_DESIGN.ja.md)（採否は未定）と [tests/BENCH.ja.md](tests/BENCH.ja.md)。移行の順番は TEST_DESIGN の 9 章。

- [ ] **TST-01 現状の照合**: 計画・coverage・library 実装・runner・実行 artifact の不整合を一覧化する。
  各契約を再利用/作り直し/対象外候補に分け、理由を書く。文書上の実機確認と今回の未測定を混同しない。
  2026-10-29: test-coverage の「sleep/RTC API 未実装」「CDC/HID 等の sketch USB class 未実装」を直した。
- [ ] **TST-05 機材台帳と配線**、**TST-06 初回測定**（実機が要る）: 内容は移した記録の「次の検討TODO」。

## コアの再設計と実行モデル

実行モデルは [ADR-0016](docs/adr/0016-execution-model.ja.md) で決まった。作業計画は [CORE_REDESIGN_PLAN.ja.md](CORE_REDESIGN_PLAN.ja.md)。
TODO にあった理想像・用途調査・実行基盤の比較材料は [planning/core-redesign/todo-carried.ja.md](planning/core-redesign/todo-carried.ja.md) に移した。

- [ ] H417 の 2 コア（V5F）をどう使うか（一方だけ、役割固定、コアごとのイベントループなど）。測定は docs/h417-multicore.ja.md、設計はまだ。
- [ ] イベントの payload の持ち方（コピー・借用・所有の移転・共有 pool）と、複数の購読者への配達で誰が解放するか。移した記録の「イベントのデータ・順序・配送保証」。
- [ ] 代表ユースケース「X035 の USB PD シンクと PPS の操作画面」（移した記録）。PD の充電器・画面・ボタンが要る。

## 永続化

決めてコアに入れた（PERSIST-01、[docs/flash-store.ja.md](docs/flash-store.ja.md)）。

- 残り（必要になったら）: キーと値の保存、内蔵フラッシュ上のファイルシステム（大きい系列だけ）、書き込みツールの既定の消去範囲。

## examples

例を足すときの決まりは [docs/examples-build-rules.ja.md](docs/examples-build-rules.ja.md)。

- [ ] 実機での確認がまだのもの: 第1弾（UART の操作、PWM の波形、I2C の対向通信・相手不在・復旧、外部 SPI Flash の ID）、
  第2弾（ボタンの押下・長押し・解放、可変抵抗と PWM、USB の再接続時の中立、ゲームパッドの軸・両端・中立帯を PC の確認画面で）。
- [ ] ベンチの CH32X035C8T6 で `analogRead()` が端子（A0）でも内部の基準電圧でも 0 近く（3〜9）を返す（2026-10-28、18c97fee でも同じなので以前から）。
  `touchRead()` を 1 回挟むと値が出るが、A0 と Vref が同じ値（約 800）になり、正しい読みとは思えない。ADC のレジスタは前後で同じ。
  ADC をリセットして EVT と同じ手順（較正なし、/6・/8・/16、較正あり）でレジスタを直接読んでも、A0・A1・Vref（ch15）のどれも 20〜50。
  ソフトではなく基板か個体（アナログの電源など）を疑う。回路図を見るか、別の X035（WeAct の基板など）で同じスケッチを試す。
- [ ] V103R8T6（d39e、CH549 の Link）が休止の試験の最初の休止から起きず、デバッガも応答しない（2026-10-28）。Link では電源を切れず、
  ハブの口の電源でも戻らない。手で電源を入れ直してから、RTC（LSI の測定・アラーム）を調べる。それまで V103 は RTC の系列から外している。
- [ ] まだ無い例（下の表 A）: `RgbLed`、`SerialBridge` / `PeripheralRoutes`（配線が要る）、`I2CEeprom`・`SpiSharedBus`（部品が要る）、
  `SoftI2CRegisterRead` / `SoftSpiDevice`、USB 系（`MidiChannelsAndReceive`、`UsbReconnect` / `VendorBulkPeer`、Host の
  HID / hub）、`PpsVoltageControl`、`TlsClient`（TLS の例が 1 つも無い。実機の Ethernet と V307 の RAM の確認が要る）、`UsbNcmNetwork`、
  `AdcBlockStatistics`、`SensorConsole`。
- [ ] 応用の候補: ESP32 の HTTPClient / WebServer や EVT の MQTT を参考にした HTTP 取得・小さな Web server・telemetry、
  SDK の ETH↔UART 中継、audio 録音、camera 表示。TCP / TLS があるだけで HTTP / MQTT の公開 library もあるとは扱わず、
  library の採用とメモリ・接続復旧を先に確認する。
- ESP32のWi-Fi / BLE / ESP-NOW / Matter / Zigbee / OpenThread / RainMaker、ULP、専用のOTA / provisionerは、
  同名例をそろえるためには追加しない。CH32の`EVT(CH32V20x):BLE`も含め、対応ハードウェア・通信stack・公開APIの
  採用を先に判断し、必要な用途が決まったら別項目にする。
- EVTのFreeRTOS / RT-Thread等のOS移植、CoreMarkやボード固有の製品デモは、一般向けexamplesへ一括で取り込まない。
  OS向けのqueue / semaphoreの題材はTaskKit / SyncKitの実行モデルで学べる形へ直し、計測や内部検証は`tests/`側へ置く。

### 比較元と選び方

- このコアの `libraries/*/examples` と公開ヘッダを確認し、同じ機能の例がある場合は、その例で扱わない使い方を候補にした。
  `tests/` の検証コードは参考にできるが、配線・操作・結果を説明する利用者向け examples の代わりとはしない。
- 表中の `ESP:` は、指定された
  `~/.arduino15/internal/esp32_esp32_3.3.12_feadd113249efafd/libraries/` からの相対パス。
  `EVT(CH32系列):` は `~/dev_wch/CH32系列/EVT/EXAM/` からの相対パスで、例のディレクトリを示す。
  調査した SDK は CH32V003 / V006 / V103 / V20x / V205 / V307 / V407 / X035 / X315 / L103 / M030 / H417。
  同じ機能が複数の SDK にあるときは代表例だけを記す。これは全系列で利用可能という意味ではない。
- **A は既存 API で例を組み立てる候補、B は公開 API・ドライバ・ライブラリの採否や拡張も先に検討する候補。**
  SDK に例があるだけで、Arduino の API があるとは扱わない。実装側の検討は
  [FEATURE_CANDIDATES.ja.md](FEATURE_CANDIDATES.ja.md) と対応付け、着手時にソースと対象ハードウェアで確かめる。
  サポート状況・測定値・版ごとの制約はこの一覧へ複製せず、各ライブラリの説明へ集約する。
### A. 既存 API を使って追加する例

| 機能 / スケッチ案 | 既存例に足りない使い方・例で示すこと | 比較元・参考例 |
| --- | --- | --- |
| PWM：`PwmFrequency` / `RgbLed` | `analogWriteFrequency()`で周波数とdutyを別に設定し、同じタイマを共有するピンへの影響を観察する。RGB LEDの配線、反転、明るさ補正を示す。 | `ESP:ESP32/examples/AnalogOut/ledcFrequency`、`ESP:ESP32/examples/AnalogOut/ledcWrite_RGB`、`EVT(CH32V003):TIM/PWM_Output`。LEDC専用APIは移さない。 |
| 複数UART・ピン経路：`SerialBridge` / `PeripheralRoutes` | UART同士の双方向中継と送信詰まりを扱う。`begin(..., rx, tx)`・`setPins()`・`setRoute()`で、選べる経路と不正な組合せを示す。Wire / SPIにも同じ考え方を展開する。 | `ESP:SPI/examples/SPI_Multiple_Buses`、各公開ヘッダのピン経路API。必要な周辺回路数はビルド条件で絞る。 |
| I2C master：`I2CRegisterRead` / `I2CEeprom` | `I2CScanner`の次に、レジスタ番号を書いて`endTransmission(false)`から`requestFrom()`する読み取り、返却長とNACKの確認、EEPROMのページ境界と書き込み完了待ちを示す。 | `ESP:Wire/examples/WireMaster`、`EVT(CH32V003):I2C/I2C_EEPROM`。 |
| SPI実機通信：`SpiFlashId` / `SpiSharedBus` | `SPILoopback`の次に、外部SPI FlashのID等を読み、CS、mode、周波数、ビット順を説明する。複数CSを持つ同一バスと、複数SPIバスを区別し、機器ごとにtransactionを組む。 | `ESP:SPI/examples/SPI_Multiple_Buses`、`EVT(CH32X035):SPI/SPI_FLASH`。最初のFlash例は消去・書き込みを行わない。 |
| ソフトウェアバス：`SoftI2CRegisterRead` / `SoftSpiDevice` | スキャン・ループバックだけでなく、同じ部品をWire / SPI版とソフトウェア版で使う。ピン選択、共有GND、pull-up、CPUを使う転送の違いを比較する。 | 既存 `SoftI2CScanner` / `SoftSPILoopback`、上のI2C / SPI実機通信例。 |
| USB MIDI：`MidiChannelsAndReceive` | MIDIチャンネルとUSB cable番号を区別し、受信メッセージでLED等を操作する。constructorでcable数を設定し、送信先cableを明示してPC上の複数portと対応付ける。 | `ESP:USB/examples/MIDI/MidiMultiChannel`、`ESP:USB/examples/MIDI/ReceiveMidi`、既存 `MIDI` / `MIDIController` / `MIDIInterface`。 |
| USBの継続動作：`UsbReconnect` / `VendorBulkPeer` | 既存のevents例・Host切断通知を踏まえ、送信待ちの扱い、切断後の状態破棄、再接続後の再開まで示す。Vendor転送はPC側の短い送受信例と組にし、部分転送と送信詰まりを再現する。 | `ESP:USB/examples/USBVendor`、既存 `SerialEvents` / `USBVendor` / `VendorBulk` / `DeviceInfo`。単なる通知表示を増やすものにはしない。 |
| USB Host HID：`HostGamepad` / `HostHidReports` | `onGamepad()`で軸・ボタンを読み、consumer / system controlやraw reportを機器ごとに識別する。既存Keyboard / Mouse例の外にあるreport descriptor、output / feature reportの用途も示す。 | `ESP:USB/examples/USBHostGamepad`、`ESP:USB/examples/USBHostHIDCombo`、既存HostのHID公開API。 |
| USB Hostの複数機器：`HostHubDevices` / `HostComposite` | hub経由の接続、複合機器の複数interface、機器ごとのaddressとportを表示し、片方を抜いても他方を扱える例にする。VBUSの供給条件、対象とするhub構成をREADMEに書く。 | `ESP:USB/examples/USBHostCDC_MSC_HID`、既存 `DeviceInfo`と`getDevices()` / `getHubInfo()`。 |
| USB PD：`PpsVoltageControl` | `ListProfiles`・`CycleVoltages`の次に、PPSの範囲を確認して`requestProfile()`へ電圧・電流を渡す。拒否、PPSのない充電器、抜き差しを扱う。入力処理を状態機械にしても、契約要求API自体は待つ呼び出しである点を説明する。 | `EVT(CH32X035):USBPD/USBPD_SNK`、既存PD例と公開API。電圧が実際に変わるので、負荷と基板の許容範囲を先に明示する。 |
| ネットワーク導入：`EthernetDhcp` / `UsbNcmNetwork` | `Network`の入口として`ETH`またはUSB NCMでリンクとIP取得を確認し、固定IP、DHCP、切断・再接続、待ち時間の上限を示す。`UsbNetworkPing`の手作りARP / ICMPから、IPスタックを使う例へつなぐ。 | `ESP:Ethernet/examples/ETH_LAN8720`、`EVT(CH32V307):ETH/DHCP`、既存 `UsbNetworkPing`。ESP32向けPHY設定ではなくCH32側の接続方法で書く。 |
| TLS：`TlsClient` / `MutualTlsClient` | `NetworkClientSecure`でCA検証、ホスト名、接続前の時刻確認、エラー表示を示す。次段階でクライアント証明書を扱い、証明書・鍵の寿命と保管先を説明する。固定の外部サービスや期限付き証明書への依存を避け、ローカル相手の準備手順を付ける。 | `ESP:NetworkClientSecure/examples/WiFiClientSecure`、既存`NetworkClientSecure`の公開API。通信路とメモリの実機確認を前提にし、未検証の組合せを動作済み例として配らない。 |
| 取り込みデータの利用：`AdcBlockStatistics` / `LogicPulseDecode` | `HsAdcStream` / `LogicStream`の取得APIを使い、平均・最小最大やパルス幅の解析へ進む。取得側と処理側を分け、バッファの所有期間、処理落ちの検出、停止・再開を示す。 | 既存 `HsAdcRead` / `HsAdcStream` / `LogicBurst` / `LogicStream`、`EVT(CH32H417):HSADC/HSADC`。 |
| タスク・同期の応用：`SensorConsole` | 個々のプリミティブの例から、周期計測・コマンド受付・表示の組合せへ進む。ISRから通知して、短い処理を順に進め、キュー満杯時にも止まらない例にする。 | `ESP:ESP32/examples/FreeRTOS/Queue`、既存TaskKit / SyncKit例。FreeRTOSのスレッドモデルを移すのではなく、このコアの実行モデルで構成する。 |

済み（表から外した）: `ButtonDebounce` / `AnalogInputToPwm`、`SystemInfo`、`ResetReason` / `WatchdogRecovery`、`InternalAnalogRead`、`PwmFrequency`、`SerialLineReceiver` / `SerialPacketReceiver`、`I2CRegisterRead`、`I2CSlaveRegister`、`I2CTimeoutRecovery`、`SpiFlashId`、`ServoPulseControl`、`ButtonKeyboard` / `ButtonMouse` / `AnalogGamepad`、`EthernetStatus`、`TcpEchoServer` / `UdpEcho` / `HttpGet`、`NetworkTime` / `MdnsWebServer`、`LogicPulseDecode`。

### B. API・ドライバ・ライブラリ整備と組にする例

ここは「SDKにあるのに例がない」機能を見落とさないための候補。採用を決めたものから公開APIを設計し、
Arduinoのピン指定・タイマ所有・割り込み・転送バッファの規約に合わせる。
SDKの初期化をそのまま貼り付けて、コアの管理する周辺回路を上書きするスケッチにはしない。

| 機能 | 追加したい例と先に決めること | 比較元・参考例 |
| --- | --- | --- |
| ADCのハードウェア連携 | タイマ / 外部入力による開始、injected変換、analog watchdogのしきい値通知、dual ADC。トリガ・DMA・タイマの取得と解放を定義する。 | `EVT(CH32V307):ADC/TIM_Trigger`、`EVT(CH32V003):ADC/AnalogWatchdog`、`EVT(CH32V307):ADC/DualADC_RegSimul`。 |
| ADC前段・OPA・CMP | 入力増幅、比較器のしきい値、比較結果の割り込み・PWM停止。入力電圧範囲、出力経路、対応するアナログ回路のAPIを決める。 | `EVT(CH32L103):ADC/ADC_PGA`、`EVT(CH32V006):OPA/OPA_PGA`、`EVT(CH32V006):OPA/CMP_BKIN`。 |
| DAC | 直流出力からタイマ同期の波形出力・DMAへ進む。PWMとの違い、更新率、バッファの寿命を示す公開APIを用意する。 | `EVT(CH32V307):DAC/DAC_Normal_OUT`、`EVT(CH32V307):DAC/DAC_DMA`、`EVT(CH32V307):DAC/DualDAC_SineWave`。 |
| タイマ出力・同期 | one-pulse、output compare、複数タイマ同期、相補PWM・dead time・break。周期callbackは既存`TimerResource`を入口にし、簡易Tickerが必要かは別途判断する。 | `ESP:Ticker/examples/TickerBasic`、`EVT(CH32V003):TIM/One_Pulse`、`EVT(CH32V003):TIM/ComplementaryOutput_DeadTime`、`EVT(CH32V003):TIM/Synchro_Timer`。 |
| 低消費電力タイマ | LPTIMの低電力中の周期起床、PWM / one-pulse。通常タイマ、`millis()`、復帰後の時間の関係を定義する。 | `EVT(CH32L103):LPTIM/LPTIM_LP_WakeUp`、`EVT(CH32L103):LPTIM/PWM_OnePulse_SingleMode`。 |
| UARTの高速受信 | DMA + idleによる可変長受信、二重バッファ、エラーの通知。受信callbackの実行文脈、未処理データの所有権を先に決める。 | `ESP:ESP32/examples/Serial/OnReceiveError_BREAK_Demo`、`EVT(CH32V307):USART/USART_DMA`、`EVT(CH32V307):USART/USART_Idle_Recv`。 |
| UARTの専用モード | RTS / CTS、RS-485の送信方向制御、half-duplex / single-wire、同期転送。外付けtransceiverと電気的な接続を分けて説明し、必要なAPIを選ぶ。 | `ESP:ESP32/examples/Serial/HardwareFlowControl_Demo`、`ESP:ESP32/examples/Serial/RS485_Echo_Demo`、`EVT(CH32V003):USART/USART_HalfDuplex`、`EVT(CH32V307):USART/USART_SynchronousMode`。 |
| ソフトウェアUART | 短い送受信と他処理との共存。採用するライブラリ、タイミング制約、受信割り込み・タイマ競合を検討する。 | `EVT(CH32V003):APPLICATION/SoftUART`。 |
| SPIの専用モード | slave受信とmaster側の対になる例、DMA、hardware NSS、half-duplex、CRC。既存SPI APIの拡張か別APIか、対応範囲を決める。 | `EVT(CH32V003):SPI/SPI_DMA`、`EVT(CH32V003):SPI/FullDuplex_HardNSS`、`EVT(CH32V003):SPI/1Lines_half-duplex`、`EVT(CH32V307):SPI/SPI_CRC`。 |
| I2Cの追加モード | DMA、10 bit address、PEC、必要なら複数master。通常Wireとの相互運用・未対応時の扱いを決め、ニーズのあるモードから例にする。 | `EVT(CH32V003):I2C/I2C_DMA`、`EVT(CH32V003):I2C/I2C_10bit_Mode`、`EVT(CH32V307):I2C/I2C_PEC`。 |
| I3C | controllerによる機器列挙・転送、対応する場合のIBI / target。I2Cの例と分け、専用driverと公開APIを検討する。 | `EVT(CH32V407):I3C/I3C_DMA_Controller`、`EVT(CH32H417):I3C/I3C_Controller_Check_ZeroIBI`、`EVT(CH32H417):I3C/I3C_DMA_Target`。 |
| CAN / CAN FD | 2台での送受信、filter、bus-offからの復帰。transceiver・終端抵抗と対応形式を説明する。共通`HardwareCAN`の型だけでなく実際のdriverが必要。 | `ESP:ESP32/examples/TWAI/TWAItransmit`、`ESP:ESP32/examples/TWAI/TWAIreceive`、`EVT(CH32V307):CAN/Networking`、`EVT(CH32L103):CAN/CANFD`。 |
| I2S / SAI / DFSDM | tone出力、mic収録、codec loopback、デジタルfilter。USB Audioの生成データと区別し、DMAとaudio bufferのAPIを設計する。 | `ESP:ESP_I2S/examples/Simple_tone`、`ESP:ESP_I2S/examples/Record_to_WAV`、`EVT(CH32V307):I2S/I2S_DMA`、`EVT(CH32H417):SAI/SAI_Play`、`EVT(CH32H417):DFSDM/DFSDM_I2S_Audio`。 |
| ファイルシステム・SD | ファイル作成・追記・読み戻し、ディレクトリ、取り外し、満杯時の扱い。SPI / SDIO / SDMMC / eMMCのblock転送と、ファイルシステムの採用を分けて決める。 | `ESP:SD/examples/SD_Test`、`ESP:LittleFS/examples/LITTLEFS_test`、`EVT(CH32V307):SDIO/SDIO_SD_FATFS`、`EVT(CH32H417):APPLICATION/FATFS_eMMC`。既存`MSCSdCard`はファイル操作の例ではない。 |
| USBストレージとファイルの共有 | Host MSCからファイルを読む、SDをDevice MSCとして公開する際の所有権切替。PCとスケッチが同時に同じFSを書き換えない構成を決める。 | `ESP:USB/examples/USBHostMSC_Test`、`ESP:SD_MMC/examples/SD2USBMSC`、既存 `MSCBlockDump` / `MSCSdCard`。FSとHost側block deviceの接続が前提。 |
| 更新・bootloader連携 | ファイルからの更新、USB DFU / Firmware MSC、失敗時の復旧。書き込み範囲、イメージ検証、boot手順を確定してから例にする。 | `ESP:USB/examples/FirmwareMSC`、`ESP:Update/examples/SD_Update`、`EVT(CH32V003):USART_IAP`。既存`DfuRuntime`による更新側への移行と、実際の書き換え処理を区別する。 |
| 外部メモリ | QSPI FlashのID・読み出しからmemory mapへ、FMC / FSMCのSRAM / SDRAM、PSRAMへ進む。起動時設定・リンカ・DMAが参照できる領域を決める。 | `EVT(CH32V205):QSPI/QSPI_FLASH`、`EVT(CH32H417):QSPI/QSPI_MemoryMap`、`EVT(CH32H417):FMC/SDRAM_16bit_DMA`、`EVT(CH32V407):PSRAM/PSRAM`。 |
| カメラ・表示 | DVPからのframe取得、SPI LCD、LTDC表示、GPHAによる描画。sensor / panel driver、frame bufferとメモリ要件を定義し、その後USB Videoへ実映像を渡す応用を作る。 | `ESP:ESP_Video/examples/dvp_camera`、`EVT(CH32V307):DVP/DVP_TFTLCD`、`EVT(CH32V103):SPI/SPI_LCD`、`EVT(CH32H417):LTDC/LTDC_Display`、`EVT(CH32H417):GPHA/GPHA_M2M_WithLCD`。既存`VideoCamera`はテスト画像の例。 |
| 精密な波形入出力・PIOC・ARGB | WS2812、IRの送受信、1-wireを題材にし、PIOC実行codeや専用driver、DMA / タイマとの関係を整理する。ESP32 RMTのAPIの移植とは分ける。 | `ESP:ESP32/examples/RMT/RMTWrite_RGB_LED`、`EVT(CH32X035):APPLICATION/WS2812_LED`、`EVT(CH32X035):PIOC/PIOC_NEC`、`EVT(CH32X035):PIOC/1_Wire`、`EVT(CH32X315):ARGB/Gradient`。 |
| CRC・乱数・hash | CRCの既知データ検算、RNGの失敗処理、採用した暗号libraryによるhash計算。`random()`と鍵生成用乱数を混同しない公開窓口を検討する。 | `ESP:Hash/examples/SHA2`、`EVT(CH32V307):CRC/CRC_Calculation`、`EVT(CH32V307):RNG/RNG`、既存 `RandomNumbers`。CRC周辺回路だけで暗号hashを実装できるとは扱わない。 |
| 電源・保護・診断 | PVD通知、電源低下からの復帰、WWDG、Flash / RAM ECC診断。通常のIWDGやリセット理由の例と分け、保護設定の寿命と回復手順を設計する。 | `EVT(CH32V003):PWR/PVD_VoltageJudger`、`EVT(CH32V003):WWDG`、`EVT(CH32M030):FLASH/FLASH_ECC`、`EVT(CH32H417):ECDC/RAM_Block`。 |
| clock・RAM実行・高性能コアの資源 | clock出力・校正、RAM実行、cacheとDMAの整合、IPC / HSEMによるコア間データ交換。コアの起動・クロック設定との境界、共有メモリと同期APIが前提。単独コアのTaskKit例で代用しない。 | `EVT(CH32V003):RCC/MCO`、`EVT(CH32M030):RCC/HSI_Calibration`、`EVT(CH32X035):RunInRam/RunInRAM_Select`、`EVT(CH32H417):CPU/ICache`、`EVT(CH32H417):CPU/IPC`、`EVT(CH32H417):CPU/HSEM`。 |
| 特殊な高速・専用インタフェース | UHSIF転送、SerDes、SWPMIの最小通信と相手側の例。必要なボード・相手機器が用意できる用途から、専用driverとAPIを検討する。 | `EVT(CH32H417):UHSIF/UHSIF_SLAVE`、`EVT(CH32H417):SerDes/FullDuxTrans`、`EVT(CH32H417):SWPMI/SWPMI_SwpCard`。 |

済み（表から外した）: 通常 ADC の連続取得（CH32RVAdcStream）、タイマ入力（CH32RVTimerInput）、RTC、sleep / standby、フラッシュへの設定保存（`FlashSettings`）、touch 入力（`TouchRead`）。

## USB

CH32RVUsbDevice / CH32RVUsbHost は自作のスタック（[ADR-0012](docs/adr/0012-usb-stack.ja.md)）。全系列のドライバはあり、実機で確かめたのは X035 と H417。

- [ ] 新しい系列の実機確認（ベンチの DUT の USB 端子を配線してから）。HSI から作る 48 MHz の精度、V103 の EP4〜7 の
  自動トグル、HS の PHY と HSE。第2世代 USBHS（V407 / V467、X305 / X315、V205）と X315 の USBSS はビルドだけ。
- H417:
  - [ ] USBSS のデバイスの実機の続き（列挙と CDC、DFU までは確認済み）。
  - [ ] USBHS のドライバ。PB8 / PB9 が SWD と兼用なので、デバッガを外した状態での確認が要る。
  - [ ] UART ISP（PB6/PB7）: ソフトのリセットでは入らなかった（docs/h417-bench.ja.md）。電源の入れ直しは未確認。入れるかどうかは別に決める。
  - OEP の probe にする件は、コアの側は入れた（84b57b0）。oep-probe-arduino への依頼は 2026-10-21 に dev-oep のセッションへ送った（向こうの状況は未確認）。
- **USBSS の host（H417）を実装する**（2026-10-28 にユーザーの依頼。後回し）。常設ベンチ（tests/BENCH.ja.md 7 章）で、SS の試験を
  H417 どうしで組むために要る（DUT が device のとき相手の H417 が host、DUT が host のとき相手の H417 が device）。
  - 材料: EVT `USBSS/HOST/Host_SS_Access_Sector`（ほかにハブ経由の 2 例）。`Common/ch32h417_usbss_host.c` がリンクの初期化と
    LMP の割り込み（PORT_CAP → PORT_CFG → PORT_CFG_RES で接続、RX_DET で切断、warm reset で初期化し直し）、
    `ch32h417_enum.c` の `U30HostCtrlTransfer` が SETUP / データ / STATUS の各段（`UH_TX_CTRL` / `UH_RX_CTRL`、
    シーケンス番号と burst の数、NRDY のあと ERDY を待って送り直す、STATUS の前に 1 ms の待ち）。ソースで読める
    （`libRV3UFI.a` は U ディスクのファイルシステムで、使わない）。
  - 要るもの: (1) 新しい HCD（`CH32RVUsbHcd` の 2 つ目。今は USBFS だけ）。リンクの割り込み、転送、シーケンス番号、
    ERDY / NRDY。(2) host の列挙の SS 対応（EP0 の 512 バイト、BOS、SET_SEL、SET_ISOCH_DELAY、エンドポイントの
    companion 記述子と burst、速さの値 SuperSpeed）。(3) USBHS の host（SS の口の USB 2.0 側。今は無い）は別の項目にする。
  - 確かめ方（2026-10-28 に決定）: 作る間は市販の USB 3.0 のメモリで手作業の試験（host の MSC クラスで読む。EVT と同じ形）。
    安定したら、H417 どうしのピア試験にする（H417 は 2 台を追加で確保中）。H417 の HS の線を切り離す部品も確保中で、
    切り離せば SWD で書き換えられる（SS の口を挿したままでも DFU に頼らない）。
- [ ] ファイルシステム（docs/filesystem.ja.md）の残り: SD での実測（ベンチに SD が要る）、FAT を NOR に載せる摩耗分散の層、
  littlefs の設定（block_cycles、媒体の大きさ）を変えたときの値。
- [ ] IP（docs/network-stack.ja.md）の残り: V407 / V317 / V208 の実機。V208 / V203RB は CPU Clock の `60MHz (Ethernet, no USB)` を選ぶ。
  V307 のチップのリビジョン 0x20 の回避策。対象の系列の USB デバイスのドライバで NCM を実機で確かめる。HTTP サーバー、MQTT / HTTP のクライアント（FEATURE_CANDIDATES の NET-03）。
- [ ] **V307 / V317 の HTTPS のヒープ**（RAM 64 KB で TLS のハンドシェイクが足りない）。`RAM_CODE_MOD`（Flash と RAM の分け方）のメニューが要りそう。ユーザーと決める。
  - ほかの系列の `enterBootloader()`（工場出荷 ISP への入口）。X305 / X315 は X035 と同じ手順を入れた（2026-10-22、ビルドのみ）。
    H41x は試して失敗（docs/usb.ja.md）。V20x / V30x / V103 / L103 / V205 / M030 / V4x7 は BOOT_MODE が無く、BOOT0 の端子だけ。
  - デバッガ復帰時の `SystemInit()` のやり直しで USB のクロック設定が消える件の対策。
  - **UF2（MscFirmwareDisk）での更新はやる**（2026-10-20）。ほかのファームウェア更新のクラス（EspUsbDevice の
    FirmwareUpdate、DFU の Download、CDC / Vendor / ネットワーク経由）も実装予定。使わなければ費用がかからない形にする。
    UF2 は仮想の FAT を見せるだけなので、ファイルシステムのスタックを待たずに作れる。優先度は低いまま。Flash の書き換えの
    土台は「永続化」の項で作るので、それと USB スタック・タスク周りの入れ替えのあとに取りかかる。工場出荷の ISP より得があるのは、読み出し保護をかけたまま更新
    したいとき、道具を入れずにドラッグ＆ドロップ（UF2）で更新させたいとき、署名の確かめや A/B の巻き戻しが要るとき、
    USB 以外（ネットワークなど）から更新したいとき。下地に Flash の自己書き換えが要る。
  採否は FEATURE_CANDIDATES の UPD-01 / UPD-02 に記録した。
- [ ] EspUsbDevice との差の残り: Info の例 3 本の移植、ホストの Audio / Video / Network、複数リスナー、キュー、64 ビット LBA、CCID の追加 API、例 30 本ほど。
- DFU_DETACH の応答がホスト側で STALL になる原因を調べる。見込み（2026-10-21）: ブートローダーへ移る前の 20 ms の
  待ちが、印に `millis() | 1` を使っていたため 0 ms になることがあり、ステータス段の応答の前に抜けていた
  （85f96bb で直した）。H417 では、その後の DETACH → DFU を 10 回ほど続けて通した。PC ホストの X035 で確かめれば閉じられる。
- ホストで未対応: アイソクロナス（オーディオ・ビデオ）、USB ネットワーク（NCM / ECM）、MSC のファイルシステム、
  ハブ越しの低速デバイス（PRE PID の実機確認）、V103（旧 USBFS）と USBHS のホスト。ESP32-S3 側の癖は tests/peer/usb/README.ja.md。
- UIAPduino のソフトウェア USB（rv003usb）をアプリモードでも動かす件は FEATURE_CANDIDATES の USB-09。

## USB PD ソース（CH32RVUsbPd）の実機確認

（2026-10-28）PD の試験は基本は手作業。常設ベンチに置くなら X035 どうしで、片方の CC の抵抗を外してソースにし、シンクとソースのピア試験にする（tests/BENCH.ja.md 7 章）。

ソース（`USBPDSource`、`CH32RVUsbPdSource`）は実装済み（ビルドとフレームの単体試験まで）。残りは実機での確認。
- X035 どうしの対向（シンク `USBPD` ⇔ ソース `USBPDSource`）。ソースにする WeAct の板は CC の 5.1 kΩ（R4 / R5）を
  外す（回路図では SB2 / SB3 で PC14 / PC15 につながる Rd。実物で確認）。試験のスケッチは両方のコールバックを
  登録して明示的に true を返す。
- 確かめること: Rd の検出と向き（表裏）、固定と PPS の契約、Get_Source_Cap、Soft_Reset / Hard_Reset、PPS の途絶
  （15 秒）、Reject（名乗っていない電流）、取り外し、コールバックが false を返したとき（VBUS: 接続しない、電圧:
  Hard Reset で 5 V に戻る）。
- 実際の電圧が出ているかは範囲外（基板側の仕事）。

## 系列で限られているもの

[docs/peripheral-support.ja.md](docs/peripheral-support.ja.md) の ○ / ◐ の中身と、[docs/peripheral-plan.ja.md](docs/peripheral-plan.ja.md) の B（後で）。

- [ ] V205・X305 / X315 の Wire（プルアップの配線が要る）と、既定以外の route。V205 は LinkE の接続で s0 が壊れるので、結果はリセットごとに 1 回だけ読む。
- [ ] V20x / V30x の ADC の校正値（EVT の Get_CalibrationValue）。V307 ではその値が 1728 で、当てると VDD が 2.64 V になり不自然（当てない今は 3.19 V）。外部の電圧計と照合して決める。
- [ ] クロック: H41x と X305 の CPU Clock のメニュー、HSE、各項目の実機確認（UART の速度・ADC・USB）。L103 の 72 / 96 MHz は、OEP probe（RP2350）が動いている L103 に接続できない（試すイメージには `CH32RV_BOOT_HOLD_MS` を付ける、docs/upload-routes.ja.md）。
- PD の他系列（L103 / M103、V205、X305 / X315、H41x、M030 の USBPD0）: 2026-10-22 に入れた（ビルドのみ）。BMC の
  クロックは F_CPU から計算する（EVT の表は 10〜240 MHz。8 MHz の L103 / V205 / M030 で通るかは実機で確かめる。
  試験は `--case pd`）。M030 の USBPD1 は未対応。
- PWM の remap 先と CHxN。TIM8〜10 の PWM は 2026-10-22 に入れた（V30x・V4x7・H41x、TIM1〜5 の端子は奪わない、ビルドのみ。`--case logic` の `logic_pin` でその端子を指定して確かめられる）。
- [ ] WWDG、GPIO 以外の EXTI（peripheral-plan の B）。
- [ ] SPI のスレーブ（CH32RVSPISlave）の残り: ハードウェアのマスターでの上限のクロック、V407（PA7 が RMII と重なる可能性）・H417 の実機、SPI マスターの LSBFIRST が V003 / X035 で正しいか。
- [ ] ADC の連続取得（CH32RVAdcStream）を一番遅い段より遅い一定のレートで（タイマーのトリガ。系列ごとの EXTSEL の対応が device-data に無い）。
  - Wire のスレーブ: AVR の twi に合わせた（2026-10-27）。書き込みのあと repeated START で読みが続くと、`onReceive` が先、
    `onRequest` が後（レジスタの番地を書いて読む形。Wire の I2CSlaveRegister の例がこの形）。読むだけの転送の STOP では
    `onReceive` を呼ばない。番地一致と最後の受信バイトが同時に立ったときも、そのバイトを `onReceive` に入れる。ビルドだけで、
    実機の確認は未（OEP の治具は I2C のターゲットにしかなれず、マスター役の配線がベンチに無い）。
- `analogReference()` はハードウェアに基準電圧の選択が無い。
