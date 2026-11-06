# TODO

決まったことは ADR や各文書に移し、ここからは消します。

この TODO は個別の検討事項と予備調査を保持します。

クラス・主要動作モードの追加候補と、現行で対象外の機能の採否検討は
[追加機能の採否検討](FEATURE_CANDIDATES.ja.md) にまとめます。


## テスト計画

計画は [tests/TEST_PLAN.ja.md](tests/TEST_PLAN.ja.md)、常設ベンチの案は [tests/BENCH.ja.md](tests/BENCH.ja.md)。

- [ ] **TST-01 現状の照合**: 計画・coverage・library 実装・runner・実行 artifact の不整合を一覧化する。
  各契約を再利用/作り直し/対象外候補に分け、理由を書く。文書上の実機確認と今回の未測定を混同しない。
  2026-10-29: test-coverage の「sleep/RTC API 未実装」「CDC/HID 等の sketch USB class 未実装」を直した。
- [ ] **TST-05 機材台帳と配線**、**TST-06 初回測定**（実機が要る）。

## コアの再設計と実行モデル

実行モデルは [ADR-0016](docs/adr/0016-execution-model.ja.md) で決まった。

- [ ] H417 の 2 コア（V5F）をどう使うか（一方だけ、役割固定、コアごとのイベントループなど）。測定は docs/h417-multicore.ja.md、設計はまだ。
- [ ] イベントの payload の持ち方（コピー・借用・所有の移転・共有 pool）と、複数の購読者への配達で誰が解放するか。
- [ ] 代表ユースケース「X035 の USB PD シンクと PPS の操作画面」。PD の充電器・画面・ボタンが要る。

## 永続化

決めてコアに入れた（PERSIST-01、[docs/flash-store.ja.md](docs/flash-store.ja.md)）。

- 残り（必要になったら）: キーと値の保存、内蔵フラッシュ上のファイルシステム（大きい系列だけ）、書き込みツールの既定の消去範囲。

## examples

例を足すときの決まりは [docs/examples-build-rules.ja.md](docs/examples-build-rules.ja.md)。

- [ ] 実機での確認がまだのもの: 第1弾（UART の操作、PWM の波形、ハードウェアの I2C の対向通信・相手不在・復旧、外部 SPI Flash の ID）、
  第2弾（ボタンの押下・長押し・解放、可変抵抗と PWM、USB の再接続時の中立、ゲームパッドの軸・両端・中立帯を PC の確認画面で）。
- [ ] X035 のハードウェアの I2C（Wire、ba8dc851 の repeated START を含む）を線で確かめる。ベンチの CH32X035C8T6（OEP 4-2）は、
  ロット番号の後ろから 5 桁目が 0 の、I2C と ADC の ch3 / 7 / 11 / 15 が使えない個体だった（データシート、device-data の errata
  `x035-adc-ch-i2c-unavailable`。2026-11-03 に oep が線と PVD で確認。前にここに書いた analogRead が 0 近くになる件のうち、IN15 はこれで説明がつく。A0（IN0）が 3〜9 だったのは説明がつかないが、2026-11-03 には IN2 が 3.3 V で 4044、0 V で 13 と正しく読めた）。
  ロットの後ろから 5 桁目が 0 でない C8T6 / R8T6 を用意して（ユーザーの判断）、PA10 / PA11 をプローブにつなぎ、case wire を流す。
  同じロットの F8U6 では PC16 / PC17（route 2）の I2C が通っていた（2026-09-30）ので、制限は端子によって違うのかもしれない。
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
- [ ] IP（docs/network-stack.ja.md）の残り: V317 / V203RB の実機(V203RB は CPU Clock の `60MHz (Ethernet, no USB)` を選ぶ)。V208 の USB(ベンチの暫定配線では PC につながっていない)。
  V307 のチップのリビジョン 0x20 の回避策。対象の系列の USB デバイスのドライバで NCM を実機で確かめる。HTTP サーバー、MQTT / HTTP のクライアント（FEATURE_CANDIDATES の NET-03）。
- [ ] スタックの予約（`sections.ld` の 512 B）より深く使うコードは、ヒープを黙って上書きする。TLS は、セッションの間 `_sbrk` に sp から 6 KB の余白を求めさせて直した（2026-11-03。V307 の 224k_96k の MAC エラーは、きれいなメモリー不足に変わった。H417 の HTTPS はそのまま通る）。ほかに深いスタックを使うライブラリが出たら、同じ `ch32rv_heap_stack_margin` を使う。
  V307 の eth の試験の HTTPS は、224k_96k でも 192k_128k でもメモリーが足りない（ヒープは 192k_128k で約 45 KB）。収める工夫（lwIP の大きさ、`MBEDTLS_SSL_IN_CONTENT_LEN` の縮小と max_fragment_length）は、必要になったら。

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
- [ ] SPI のスレーブ（CH32RVSPISlave）の残り（X035 で、プローブの spi-controller が 100 kHz のマスターになる spi.slave が通る。2026-11-05）: ハードウェアのマスターでの上限のクロック、V407（PA7 が RMII と重なる可能性）・H417 の実機、SPI マスターの LSBFIRST が V003 で正しいか（X035 は 2026-11-03 に case spi で、モード 0〜3 と LSBFIRST の transfer16() まで線で確かめた）。
- [ ] ADC の連続取得（CH32RVAdcStream）を一番遅い段より遅い一定のレートで（タイマーのトリガ。系列ごとの EXTSEL の対応が device-data に無い）。
- [ ] CH32RVAdcStream の複数の端子が CH32X035 で 1 つずれる（2026-11-05、periph の test_adc_stream）。バッファが値 1 つ早く始まり、
  端子 0 の場所に最後の端子の値が入る（3 本なら X, A, B, C, A, B, C…）。一番速いサンプル時間では毎回、一番遅いときはときどき。
  効かなかったこと: 止めるときに ADON を落とす、開始を SWSTART の 1 回の書き込みにする、先に並びを 1 回空で変換する、
  DMA のビットを先に立てる。ADON の書き直しだけでは変換は始まらない。1 本では起きない。ほかの系列は未確認。
  RCC で ADC1 をリセットしてレジスタを戻し、校正をやり直しても同じ（止めたときの状態が残るのではなく、開始の
  たびに起きる）。開始のあと STATR の STRT が立ったところで SWSTART をソフトで消すと、3 本の一番遅い段だけは 3 回とも
  正しく並び、ほかは毎回同じずれ方になった（決まった形になる）。2 本の一番遅い段では、High にした方の端子がどちらでも
  端子 0 の場所に来る（回転では説明できない）。EVT にはスキャンと DMA の例が無い（ADC_DMA は 1 チャネル）。
  V307 では起きない（2026-11-06。DAC1 の PA4 を Low、DAC2 の PA5 を High にして、配線なしで同じライブラリを流した。
  2 本の LH / HL、3 本の LHH / HLL が、一番速い段でも一番遅い段でも 4 回とも正しく並んだ）。ライブラリの手順は同じなので、
  X035 の ADC か DMA の振る舞いの違い。
  X035 で開始の前後を測った（2026-11-06、2 本は PA2 / PA5、3 本は PB0 も。プローブの GPIO で High / Low）:
  - 今の手順では、DMA の最初の転送が開始の直前の RDATAR の値そのもの（下位ビットまで一致、24 回中 22 回）。CNTR は
    SWSTART の直後は減らず、サンプル時間によらずポーリング 10〜14 回で 1 減る。変換 1 回分ではなく、古い値の転送。
    ADON の書き直しと SWSTART が同時かどうか、DMA ビットを先に立てるかどうかでは変わらない（どれも 24 回中 24 回ずれた）。
  - DMA のチャネルを止めたまま CTLR2 の DMA ビットを立て、そのあとチャネルを有効にして SWSTART: 古い値は入らなくなるが、
    今度は端子 0 が 2 回続く（一番速い段は毎回、一番遅い段は 3 回に 1 回）。SWSTART がもう一度開始をかけている形。
  - さらに EXTTRIG を切り、ADON の書き直しだけで開始: 2 本は 12 回中 12 回、3 本は 18 回中 17 回、同じ形に 1 つずれる
    （端子 0 の場所に最後の端子）。決まった形でないので、並びを回して打ち消すことはできない。
  次に試すこと: 不連続モード（DISCEN）や注入グループ（JSQR、JAUTO）での並び、DMA を使わず EOC の割り込みで読む場合、
  開始の前に並びを 1 周空で回してから DMA を有効にする手順。
  CH32V003（v003evt、2026-11-06）でも同じく、1 本は通り（1000 Hz に 998 Hz、段 2）、2 本は端子 0 の場所に 2 本目が入る（段 0）。
  V307 では起きない。CH32V006（v006evt、同日）では 2 本も通った（段 2、2 本目 High で 3882〜4095）。V006 は一番速い段を
  使っていないので、石の違いか段の違いかはまだ分けられない（X035 は一番遅い段でも起きる）。V006 で段 0 を指定して確かめる。
  - Wire のスレーブ: AVR の twi に合わせた（2026-10-27）。書き込みのあと repeated START で読みが続くと、`onReceive` が先、
    `onRequest` が後（レジスタの番地を書いて読む形。Wire の I2CSlaveRegister の例がこの形）。読むだけの転送の STOP では
    `onReceive` を呼ばない。番地一致と最後の受信バイトが同時に立ったときも、そのバイトを `onReceive` に入れる。
    CH32V003（v003evt、2026-11-06）で wire.slave が 6 条件とも通った。
- [ ] system の試験（test_watchdog_and_sleep）の High の長さを WireSkein で判定する（2026-11-06）。いまは states.of_file で
  区間を取り出し、条件への当てはめと長さの比べを試験の側でしている。WireSkein 66a415c で `pulses(pad, count=1,
  width_min_s=..., width_max_s=...)` が入った（区間の中で始まって終わるパルス、measured に widths_s）。PyPI に出たら版を
  上げて置き換え、high_times を消す。
- [ ] 型番を選んだときの既定の経路が USB の端子に当たることがある（2026-11-06）。generate.py の既定は、デバッグとストラップ
  （SWD・NRST・BOOT）の端子は避けるが USB の D-/D+ は避けないので、CH32X035 の小さいパッケージ（D8U6 / F7P6 / F8U6 / G8R6 /
  G8U6）では I2C1 の既定が PC16 / PC17（UDM / UDP）になる。USB を使わないなら正しく、使うなら `Wire.begin()` が USB を
  止める。避けるなら、その型番の I2C1 は既定なしになる。USB を使うかは組み立ての時にしかわからないので、避ける・避けない・
  USB を使う組み立てでだけ外す、を比べてから決める。
- 型番ごとの既定の経路（2026-11-06）: 系列の既定の経路がその型番に無いとき（端子が出ていない、またはその型番の NRST・SWD・BOOT に当たる）、
  型番を選んだ組み立てでは、その型番が出している経路から同じ規則で既定を選び直す（なければ既定なし。begin() に端子を渡す）。
  ANY は系列の既定のまま。前は型番の ABSENT（経路が 1 本も無い）しか見ていなかったので、V006E8R6 / E8R7・V005E6R6 の
  `SPI.begin()` は NRST の PC5 に SCK を出し、X035F8U6 などの `Wire.begin()` は出ていない PA10 / PA11 に出ていた。
  15 系列の 61 件（emit_misfit_defaults）。SS も同じ経路の NSS にそろえた。既定なしにする案（B）も試作したが、既定の経路が
  系列で決まらないインスタンスはすでに型番ごとに選び直しているので、それとそろう方（A）にした。
- [ ] CH32V003 の DUT が、試験の途中で「ソフトウェアのリセット」で再起動することがある（2026-11-06）。UIAPduino（30eda0ea068b
  の v003）で softspi を 7 回流して 2 回。millis() が 2134 → 65 ms、6390 → 5 ms と戻り、起動時の resetReasonName() は
  software。スケッチはリセットを呼んでいない。リセット後のスケッチは試験が設定した端子を忘れ、端子の無い SoftSPI で答えるので、
  「線に何も出ない、読みが 0」で落ちる（v003evt の最初の流しの softspi も同じ形）。どちらも ws_capture の取り込み中で、一方は
  プローブの spi-target を使う最中。DUT の外から（SWIO のデバッグの線、ch32rv の dmseq やブローカー、プローブ）リセットが入る
  道筋を [oep] と [ch32rv] に調べてもらっている。試験の側は済み（2026-11-06）: スケッチは起動で `STARTUP <理由>` を出し、
  run_hardware.py が落ちた case の dut.log から途中の STARTUP を拾って「the DUT restarted」とエラーの頭に付ける
  （tests/sketch_support/README.ja.md）。
  原因はプローブ（[oep]、2026-11-06）: dmseq のために DMCONTROL を読むとき、キャプチャと USB の DMA の負荷で P4 の CPU が
  0.3〜0.6 µs 止まり、rw のビット（長い low）が縮んで V003 は書き込みと読み、DMCONTROL に 0xFFFFFFFF（ndmreset）が入っていた。
  test_spi_contract と同じ組み合わせで 6 往復目に再現。oep-probe-arduino 695d4dc（SWIO の low を線が下がった時刻から数える）で
  144 往復リセットなし。残り: 全プローブがその版になったら、v003（UIAPduino）と v003evt で softspi を流し直して閉じる。
- [ ] v003evt の PC4 は、プローブで High にしても analogRead が 1023 中 909（同じ試験の PD2 は 1023、UIAPduino の PC4 は正しい）。
  評価ボード側の負荷とみて [oep] に確かめを頼んだ（2026-11-06）。負荷なら記録の defect（adc の lose）にする。
- [ ] Wire（ハードウェアの I2C）の `requestFrom(addr, n, false)` のあとの転送が、ときどき 5（タイムアウト）になる（CH32V003、
  v003evt、2026-11-06。n = 1〜3 のどれが落ちるかは回ごとに違い、0〜3 回）。読み終わりで CTLR1 の START が立ったまま SB が来ず、
  SCL が Low のまま（WireSkein の取り込みで 3.8 ms のあと、次の start() の間ずっと 24.9 ms）。タイムアウトの後には SB が立っている。
  SoftWire は同じ相手（プローブの i2c-target）で通る。試したこと: 3 バイト以上の読みで START を最後のバイトの後に立てる（効かず、
  戻した）。次: 1 / 2 バイトの読みでも START を立てる時期を変える、BTF・RXNE の状態と SCL を start() の前に見る、EVT に読みの後の
  repeated START の例が無いので WCH の他のコア（ch32fun など）の実装と比べる。
- `analogReference()` はハードウェアに基準電圧の選択が無い。
