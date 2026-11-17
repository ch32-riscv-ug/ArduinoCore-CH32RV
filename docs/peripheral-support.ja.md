# ペリフェラル対応表(family別)

**生成物**: `tools/generate/peripheral_matrix.py`がEVTの`EXAM/`ディレクトリ一覧から作ります。手で編集しないでください。

```
uv run --no-project python tools/generate/peripheral_matrix.py \
    --evt-root ~/dev_wch --tables ~/dev_wch/ch32-device-data --out docs/peripheral-support.ja.md
```

列はfamily(EVTの単位)ごとで、セルは**このcoreがそのfamilyで使えるか**です。siliconにあるかどうかは、EVTに**そのペリフェラルの例があるか**(例の中のサブフォルダ、たとえば`RCC/MCO`や`TIM/Encoder`も含む)と、`ch32-device-data`の`index/register_blocks.csv`に**レジスタのブロックがあるか**のどちらかで判断しています(EVTは**参照のみ**で、このrepositoryには取り込みません)。
family内のseriesで違うところ(V303にUSBHSが無い、X305にSPI1が無いなど)は備考に書きます。

「状態」と各セルはこのcoreの実装状況で、生成ではなく`peripheral_matrix.py`の`STATUS`と`CELLS`で維持管理しています。実機で確かめた範囲は[test-coverage.ja.md](test-coverage.ja.md)を見てください。

| 記号 | 意味 |
|---|---|
| ● | 使える |
| ◐ | 一部だけ使える(備考に記載) |
| ○ | siliconにあるが未実装 |
| − | 作らないと決めたもの(ユーザーの判断。今はBLEだけ) |
| 空欄 | EVTに例が無く、レジスタのブロックも無い |

**空欄は「siliconに無い」とは限りません**。EVTに例が置かれていないだけのこともあります。実装する前にreference manualか`ch32-device-data`で裏を取ってください。

| 状態 | 意味 |
|---|---|
| 実装済 | 公開APIと実装がある |
| 一部実装 | 備考に記載した範囲だけ実装がある |
| 未実装 | siliconに機能はあるがcoreの公開API/実装がない |
| 除外 | 作らないと決めたもの(ユーザーの判断。今はBLEだけ)。ほかはすべて作る。未実装の一覧と順番は[unimplemented.ja.md](unimplemented.ja.md) |

## 基本ペリフェラル

| ペリフェラル | 状態 | Arduino API | V003 | V00x/M007 | V103 | V203/V208 | V205 | V303/305/307/317 | V407/V467 | L103/M103 | M030 | X033/X035 | X305/X315 | H415/H416/H417 | 備考 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| GPIO | 実装済 | pinMode/digitalWrite/digitalRead | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● |  |
| EXTI (外部割込み) | 実装済 | attachInterrupt / `CH32RV.attachExtiLine` | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | GPIOの線はX033/X035・X305/X315が24本、他は16本。PVD・RTCのアラーム・USB/ETH/LPTIM/CMPの起床などGPIO以外の線は`CH32RV.attachExtiLine(CH32RV_EXTI_<名前>, cb, edge)`、`triggerExtiLine()`(SWIEVR)、`enableExtiLineWakeup()`(休止から起きる)。RTCのアラームの線はsetAlarm()と共有、AWUの線は休止のもので出さない。ビルドのみ(2026-11-17)。イベントモード(EVENR)は未対応 |
| PFIC (割込みコントローラ) | 実装済 | `ch32rv_irq_set_priority` | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | 優先度は`ch32rv_priority.h`の`ch32rv_irq_set_priority()`(PFICのIPRIOR、WCHの`NVIC_SetPriority()`と同じバイト)。割り込みのレベルはV30x・M030が4、H41xのV5Fが8、ほかは2。コアは優先度を書かず、自分の割り込みも0(一番急ぎ)のまま。下げた割り込みにはコアの割り込みが割り込む(契約はCH32RVのREADME)。selfapiの試験はビルドのみ(2026-11-17)。VTF・ベクタのRAM化は未実装 |
| SysTick | 実装済 | millis/micros/delay | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● |  |
| RCC (クロック) | 一部実装 | F_CPU | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | HSI(とHSI由来PLL)。ビルド時にCPU Clockのメニューで選ぶ(項目名に使えるものを書く。取得は`getCpuFrequencyMhz()`/`getApbFrequency()`、実行中に変えるAPIは無い)。既定はV103 72 MHz、V20x/V30x 144 MHz、X033/X035 48 MHz、V00x/M007 24 MHz、L103/M103 96 MHz(L103C8T6をLinkEで実機確認、2026-11-12。OEPのRP2350プローブは72/96 MHzのL103に接続できず、プローブ側で直す)、M030 72 MHz・V205 160 MHz(実機で確認してEVTの既定に)。X315は240 MHz(20 MHzも選べる。HSIからUSBHS PLL、EVTの手順の順に書く。X315MCU6で実機確認)。既定は、実機で動いた中で一番速く、USBのある系列ではUSBも使えるもの。V20x/V30xの120/144 MHzでは、ADCが14 MHzを超えないようAPB2を1/2にする(USART1・SPI1・ADCのクロックが半分になる。APB2のタイマーはHCLKのまま。internal/ch32rv_bus_clock.h)。このときだけ、その計算で100〜130 B増える。V208/V203RBのEthernetは120か60 MHz。X305はX315と同じ項目(240 MHzが既定。実機で未確認)。H415/H416/H417の項目はV5FとV3F(HCLK)の組で、400/100 MHz(既定。EVTのSetSYSCLK_400M)・200/50 MHz(HPREを1/2)・25/25 MHz(PLLなし)。どちらが`F_CPU`かはCPU Coreで決まる(200/50と25/25は実機で未確認、2026-11-17)。EVTの480 MHzの設定(USBHSのPLLをSYSCLKにする、V5F 480 MHzはコアの電圧を上げる)・295 MHz・600 MHz(HSEだけ)は出さない。HSEは下のClock Sourceメニュー |
| PWR (低消費電力) | 一部実装 | CH32RV.lightSleep / deepSleep | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | V103以外の11 familyで眠る(V103は最初のlightSleepから戻らず未対応のまま、2026-11-17、TODO)。V003・V00x・M030・X033/X035はAWU、V20x・V205・V30x・V4x7・L103/M103・X305/X315・H41xはRTCのアラームで起こす。RAMを残す深い休止(V00x・M030・X035のスタンバイ、RTCの系列のストップ。H41x・X305/X315はスタンバイが無い)、タイマー・端子(EXTI)・RTCのアラーム(`enableAlarmWakeup()`)で起床、millis()は測った分を足す(RTCの系列は端子での起床も測れる)、IWDGは休止中は止まって見える(延ばして間で餌)、deepSleep()はスタンバイのあるRTCの系列でタイマーかアラームだけ・WDTなしなら本物のスタンバイ、ほかは休止のあと再起動。M030のAWUは約150 ms単位で、端数は普通のWFIで待つ。V103・V4x7・X305/X315・M030はビルドのみ(2026-11-15、実機未確認。V103は以前最初の休止から起きなかった)。設計と実測はdocs/peripheral-a-design.ja.md 5.1 |
| FLASH (自己書き換え) | 実装済 | ch32rv_flash.h | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | 全系列で同じ関数(消す・書く・消して書く、範囲と1単位)とCH32RV_FLASH_STORE(末尾の確保)。上級者向け。推奨は外部のSD・EEPROM。EEPROM.hは無い。例はFlashSettings・FlashSettingsTwoBanks。H415/H416/H417は自分のイメージも書き換えられる(DFU runtimeが使う)。docs/flash-store.ja.md |
| USART | 実装済 | Serial | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | 全インスタンス(V205/V30x/V407/V467のUSART6〜10も、Serial9まで)。V205・X305/X315は端子ごとのAF番号を書く(V205RCT6・X315MCU6で既定のTXの波形を確認)。X033/X035は送信をポーリングで行う(割込み送信の不具合の回避)。RS485の方向制御は`setMode(UART_MODE_RS485_HALF_DUPLEX)`(DEはRTSに名指した任意の端子をコアがTCの割込みで動かす。DEを持つUSARTは無い)。受信エラーの通知は`onReceiveError()`(ノイズもUART_NOISE_ERRORで)。GPIOのUARTはSoftwareSerial(library、全系列)。IrDA・同期モード・スマートカード・LINは未実装。2026-11-17の追加分は実機未確認 |
| I2C | 実装済 | Wire | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | controllerはpolling、peripheral(slave)は割込み駆動で、1つのWireが両方を兼ねる。multi-masterのアービトレーション(負けたらやり直し)、10 bitのアドレス(`WIRE_10BIT`)、General Call、SMBusのPEC(2026-11-17、ベンチ未確認)。V205・X305/X315は端子ごとのAF番号を書く(未確認。プルアップの配線が要る) |
| SPI | 一部実装 | SPI | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | controllerはSPI。peripheral(slave)はCH32RVSPISlave(CS単位の転送、CSは任意の端子、1バイトごとに割込み。実機はV003/V103/V205/V307/X035/X315/L103、自分の端子をGPIOで叩く自己試験)。V205・X305/X315は端子ごとのAF番号を書く(V205・X315でSCKの波形とスレーブの自己試験を確認)。M103はSPI1のみ |
| ADC | 実装済 | analogRead | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | X305/X315はADC1〜4がpadを分け合うため`CH32RV_PIN_TO_ADC_INSTANCE`で切替。内部チャネルは`analogReadVref()`・`analogReadTemp()`・`CH32RV.getVddMillivolts()`(M030はシリコンに無い(RMに記載なし)、温度センサはV103/V20x/V205/V30x/V4x7/L103/M103/H41xのみ、工場の校正値の1点から換算)。`analogRead()`は変換器の値そのまま(arduino-esp32と同じ)。L103/M103のEVTと同じVDDA/2のオフセット補正は`analogReadCalibrated()`でかける(ほかの系列では`analogRead()`と同じ)。mVは`analogReadMilliVolts()`(arduino-esp32の名前。直前にVrefintで測った電源電圧で換算、M030は3300 mVと仮定。精度は未測定)。基準電圧は選べない(`analogReference()`は何もしない)。V003は10 bit。M030のソフト起動はEXTSEL[19:16]=0000(RM 8.3.3。2026-11-17までは他系列と同じ7<<17で予約値の1110を書いていた。M030のADCは実機で一度も変換していない) |
| DAC | 実装済 | analogWrite(CH32RV_DACn_PIN) |  |  |  |  |  | ● | ● |  |  |  |  | ● | V303/V305/V307/V317/V407/V467のみ。padはdevice-data由来 |
| TIM (PWM/tone) | 実装済 | analogWrite/tone | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | `tone()`はtimer割込みでpinをtoggle。使うtimerはvariantが選ぶ(`CH32RV_TONE_TIMER`)。V00x/X035/M030は空きが無く**PWMと共有**。PWMはTIM1〜5とTIM8〜10(V30x・V4x7・H41x)の、timerのチャネルが届くどの端子でも出す: 既定のピン、AFIOのremap先、V205・X305/X315・H41xの端子ごとのAF番号、相補出力CHxNの単独(反転しない)。端子の表はvariantの`CH32RV_TIMER_PADS`(device-data、既定のPWMが先)。別の経路へ移ると、そのtimerの古い経路のPWMは止まりLowになる。tone/Servoが使うTIM4/5は除く(`CH32RV_PWM_TIMER_MASK`)。既定ピンはV205のPA0・X315のPA4などで確認、remap先とCHxNはベンチ未確認(2026-11-17)。利用者向けのタイマーAPI(`timerBegin()`/`timerAlarm()`など、arduino-esp32 3.xの形、`CH32RVHwTimer.h`)とTickerライブラリ(arduino-esp32のAPI)はタイマー1本を丸ごと使う(ベンチ未確認)。timer管理はTIM1〜10(V30xのTIM8〜10、V4x7のTIM8)。入力捕捉・エンコーダはCH32RVTimerInput(library)。ETR・DMAバーストは未実装。CH32RVLogicCapture(TIM+DMAでポートを取り込む)は全系列、実機はH415/H416/H417・V205・X315 |
| LPTIM | 未実装 | - |  |  |  |  |  |  |  | ○ |  |  |  | ○ | L103・H41x |
| DMA | 未実装 | - | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | Arduino APIには露出しない。HardwareSerialの`setRxBufferSize()`/`setTxBufferSize()`(全系列、USARTごとのチャネルはvariantの`CH32RV_SERIALn_DMA_RX`/`_TX`)、CH32RVLogicCapture(全系列、実機はH41xのみ)、CH32RVHsAdc(H41x)、CH32RVAdcStream(ADC1の連続取り込み。全系列、V4x7はDMA1のチャネル12、H41xはDMAMUXでDMA1のチャネル1。実機はX035/V307/V103/V205/V407/X315/H417)が使う。チャネルの取り合いは`ch32rv_dma_claim()`(internal/ch32rv_dma.h) |
| IWDG | 実装済 | `CH32RV.wdtEnable/wdtFeed` | ● | ● | ● | ● | ● | ● | ● | ● |  | ● | ● | ● | X033/X035はLSIが無くHSI/1024で動く(X035・L103・V103・V307で実機確認)。M030はIWDGが無い |
| WWDG | 実装済 | `CH32RV.wwdgEnable/wwdgFeed` | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | PCLK1/4096/1〜8で数える7ビットのカウンタ。タイムアウト(最大`wwdgMaxTimeoutUs()`、48 MHzで約43 ms)・窓(早すぎる餌でリセット)・早期警告の割込み(コールバック)。`resetReason()`はCH32RV_RESET_WINDOW_WATCHDOGでIWDGと区別する。止められない。全12 familyでビルドのみ(2026-11-15、実機未確認) |
| RTC | 一部実装 | time() / settimeofday() / `CH32RV.setAlarm` |  |  | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ |  |  | ◐ | ◐ | V103・V20x・V205・V30x・V4x7・L103/M103・X305/X315・H41x: time()とsettimeofday()がRTC(LSIを測って1 Hz、カウンタはUNIX秒)を読み書きし、リセットとスタンバイをまたいで時刻が残る。暦はPOSIX(localtime_r/mktime)。アラームは`CH32RV.setAlarm(time_t か struct tm, callback)`(割込みで1回、1秒単位)と`enableAlarmWakeup()`(休止を終える)。休止のアラームと同じレジスタを分け合う。V20x・V205・V307・L103で時刻を実機確認(2026-10-28)、H41xも動く。V103・V4x7・X305/X315とアラームのAPIはビルドのみ(2026-11-15)。X305/X315はBDCTLRが無くRTCEN/RTCSELがRTC_CTLRLにあり、アラームはEXTIの20番。LSE・校正は未対応 |
| BKP (バックアップレジスタ) | 実装済 | `CH32RV.getBackupRegister/setBackupRegister/backupRegisterCount` |  |  | ● | ● | ● | ● | ● | ● |  |  | ◐ | ◐ | 16ビットの語をindex(0から)で読み書き。DR1〜DR4はコアのもの(RTCの印、分周、settimeofday()のずれ)なので、DR5以降を出す: V103・V205・L103/M103は6語、V20x・V30x・V4x7は6か38語(DR42が残るかを石に聞く)。VBATがあれば電源断でも残る。H41x・X305/X315はBKPのデータレジスタが無く、.noinitのRAMの16語で代わる(チェック語つき。リセットでは残り、電源を切ると0に戻る。`backupRegistersKeepPower()`がfalse)。ビルドのみ(2026-11-15) |
| CRC | 実装済 | `CH32RV.crc32` |  |  | ● | ● | ● | ● | ● | ● |  |  | ● | ● | `CH32RV.crc32()`・`ch32rv_crc32()`(ch32rv_crc.h)。CRC-32/MPEG-2(多項式0x04C11DB7、初期値0xFFFFFFFF、反転と最後のXORなし)。語の部分をハードウェアで、端数とCRCの無い系列は16項目の表で同じ値を出す。前の結果から続けられる。selfapiの試験はビルドのみ(2026-11-17) |
| ECDC (AES/SM4) | 未実装 | - |  |  |  |  |  |  |  |  |  |  |  | ○ | AES-128/192/256とSM4、ECBとCTRのみ。TLS(mbedTLS)はsoftwareで計算している |
| TIM 入力捕捉 | 実装済 | CH32RVTimerInput(library) | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | `CH32RVPulseCapture`: PWM入力モードで周期とHighの時間(割込みなし、`read()`で最新の1周期)。端子はtimerのCH1/CH2(どの経路でも)。実機はV003/V006/V103/V203/V205/V307/L103/X035/X315/H417(既定の経路)。`CH32RVInputCapture`: エッジごとの時刻(64ビット)をキュー(8個)かcallbackで、どのチャネル・経路でも(CHANGEはペアのチャネルと2つで立ち上がりと立ち下がりを別々に捕捉)。`timerPulseIn()`: `pulseIn()`をtimerの捕捉で(1 µsの刻み、選んで使う。`pulseIn()`はsoftwareのまま)。2026-11-17の追加分はベンチ未確認 |
| TIM エンコーダ | 実装済 | CH32RVTimerInput(library) | ● | ● | ● | ● |  | ● | ● | ● | ● | ● | ● | ● | `CH32RVEncoder`: エンコーダモードでx4/x2/x1、64ビットの数(桁あふれは更新割込みで数える)。名前はESP32Encoderに合わせた。timerのCH1/CH2の端子(どの経路でも、A・Bが同じtimerと経路)。実機は入力捕捉と同じ10系列(V205はEVTに例が無く欄は空だが、実機で動いた) |
| TIM 相補PWM・デッドタイム・ワンパルス | 実装済 | CH32RVTimerOutput(library) | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | `CH32RVComplementaryPwm`: CHxとCHxN(半ブリッジの上下、デッドタイムはDTGの段に丸める、ブレーキ入力BKINで両方Low、`resume()`)。`CH32RVOnePulse`: ワンパルス(`fire()`か、同じtimerのCH1/CH2のエッジでハードが開始)。`CH32RVOutputCompare`: 出力比較(ACTIVE/INACTIVE/TOGGLE/FORCE、一致のcallback)。端子はどの経路でも。非対称デッドタイム(M030・H417)、6ステップ・位相シフトは未実装。ベンチ未確認(2026-11-17) |
| PVD (電圧検出) | 一部実装 | `CH32RV.setLowVoltageDetect` / `isLowVoltage` | ● | ◐ | ● | ● | ● | ● | ● | ● |  | ● | ● | ○ | VDDが閾値(データシートの下がるときの代表値、系列ごとに4〜8段)を下回るとコールバック。休止からの起床は`enableExtiLineWakeup(CH32RV_EXTI_PVD)`。ビルドのみ(2026-11-17)。H41xはPVDの割込みが無く未対応、V004/V007/M007はdevice-dataに段が無く未対応 |
| MCO (クロック出力) | 未実装 | - | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | クロックを端子に出す。ベンチでクロックを測るのにも使える |
| USART フロー制御 (RTS/CTS) | 実装済 | setPins(rx, tx, cts, rts), setHwFlowCtrlMode() | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ESP32の形。端子はvariantの`CH32RV_SERIALn_CTS_ROUTES`/`_RTS_ROUTES`(device-dataのpinout、TX/RXのrouteのもの)。RTSは受信バッファがthresholdに達したらデータレジスタに次の1バイトを残してRTSを上げる(コアが割込みでバッファへ移すため、USARTのRTSだけでは止まらない)。CTSは端子をプルアップ。CTS/RTSの無いUSART(V30xのUART4〜8など)ではfalse。実機未確認 |
| USART 1線半二重 | 実装済 | setPins(p, p) / begin(baud, cfg, p, p) | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ESP32のone-wireの形。HDSELでTXの端子だけ、開放ドレイン(プルアップが要る)。送信中のUSARTは受信しないので、ESP32と違い自分の送信は受信バッファに戻らない。X033/X035は開放ドレインが無くプッシュプルのまま(相手からの受信は試験で飛ばす)。V003・V006・V203・V205・X035・H417で送信、X035以外で受信を確認(2026-11-17) |
| USART 受信イベント (IDLE) | 実装済 | onReceive(cb, onlyOnTimeout), setRxTimeout(), setRxFIFOFull() |  | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● |  | ESP32の形。IDLEのフラグで1シンボル、それ以上はloop()の後の配信(ch32rv_deliver.h)で数える。FIFOの代わりに受信バッファのバイト数(既定は半分)。IDLEは全USARTにある(EVTの例が無い系列も)。実機未確認 |
| USART DMA | 実装済 | setRxBufferSize(), setTxBufferSize() | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ESP32の名前(begin()の前)。その大きさのバッファをDMAが運ぶ: 受信は循環のDMAが割り込みなしで埋め続け(割り込みを止めても届く)、読む側がCNTRで位置を見る。あふれは古いものを上書きしてUART_BUFFER_FULL_ERROR。送信はwrite()が埋めたものを1回の転送で送り、次はTCの割り込みで。組み込みの64バイトまではその領域、それより大きければヒープ。チャネルはdevice-dataのdma.csv(H41xはDMAMUXで生成器が選ぶ)、ch32rv_dma_claim()でCH32RVAdcStream・CH32RVLogicCaptureと取り合い、取れない向きとRTSのフロー制御のときは割り込みの経路。全系列でビルドのみ(2026-11-17) |
| HSE (外部水晶) | 一部実装 | Clock Sourceメニュー |  |  | ◐ | ◐ | ◐ | ◐ | ◐ |  | ◐ |  | ◐ | ◐ | `clocksrc=hse`でCPU Clockの同じ周波数をEVTの前提の水晶(V00x 24 MHz、V103/V20x/V205/V30x/L103/M030 8 MHz、X305/X315 20 MHz、V407/V467 25 MHz、V208 32 MHz。EVTの回路図のY1も同じ)から作る。V208は`hse`が既定。水晶が立たなければHSIで同じ周波数。`CH32RV.clockSource()`。V003/V006/V103/V203/V205/V307/L103で実機確認(2026-10-28)。V208(2026-11-03、runtime・uart・synckit・flash・logic)でも実機確認。H41x は 25 MHz で `hse` が既定(UHSIF・HSADC の速さが HSI の約 1% のずれから正確になる。H417 で実機確認、2026-11-14: `clockSource()` が HSE、10 kHz の PWM が UHSIF で 312.5 サンプル/半周期、HSI では 315)。M030・X305/X315・V407/V467は2026-11-17に入れた(ビルドのみ、実機で未確認)。X3x5とV4x7はEVTのHSEの設定の手順をそのまま書く(X3x5のPLLはUSBHSのPLL、V4x7の60/120 MHzは水晶からのUSBHSのPLL。HSIのときはメインのPLL)。V4x7の20 MHzは水晶(25 MHz)と周波数が違うのでHSIのまま。H41xの25 MHzの項目はHSEをそのままSYSCLKにする。M030はUSBのPLL(×18)も水晶が立っていれば水晶から作る。selfcheck::test_clock_sourceで確かめる。X035にHSEは無い |
| PMP (メモリ保護) | 未実装 | - |  |  |  | ○ |  | ○ | ○ | ○ |  | ○ |  | ○ |  |
| FPU | 実装済 | float/double |  |  |  |  |  | ● |  |  |  |  | ● | ● | 単精度のFPU。`-march`のfでコンパイラが使う |
| ICache | 実装済 | - |  |  |  |  |  |  |  |  |  |  |  | ● | 起動時にEVTと同じCORECFGRを書く |
| V5F (2つ目のコア)/HSEM/IPC | 実装済 | CPU Core menu, setup1()/loop1(), CH32RV.startV3F(), CH32RV.hsem*()/ipc*() |  |  |  |  |  |  |  |  |  |  |  | ● | H417。スケッチはV5Fで動き、V3Fを2つ目のコアとして使える(例DualCoreV3F・DualCoreMessages)。V3Fは割り込みとV5Fの使わない周辺(Serial1など)を使える。HSEM・IPCの公開API(2026-11-17、ビルドのみ)。V3Fのヒープ、V3Fを外から止める仕組みは未 |
| RAM実行 | 実装済 | CH32RV_RAMFUNC | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | 関数に付けるとRAMに置く(.highcode、起動時に.dataと一緒に写す)。その大きさ分のRAMを使う。H41xは全部RAMで動く |
| AWU (自動起床) | 実装済 | CH32RV.enableTimerWakeup | ● | ● |  |  |  |  |  |  | ● | ● |  |  | 休止からのタイマー起床に使う(V003・V00x・M030はPWRの中、X035は別のブロック)。LSIで動くもの(V00x・M030)は起動後最初の休止で速さを測る。M030は分周が51000固定で約150 ms単位 |
| オプションバイト | 未実装 | - | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | 読み出し保護、NRSTの端子をGPIOにする設定など。書き込みツール(ch32rv)の範囲とも重なる |

## アナログ・タッチ

| ペリフェラル | 状態 | Arduino API | V003 | V00x/M007 | V103 | V203/V208 | V205 | V303/305/307/317 | V407/V467 | L103/M103 | M030 | X033/X035 | X305/X315 | H415/H416/H417 | 備考 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| OPA/コンパレータ | 未実装 | - | ○ | ○ |  | ○ | ○ | ○ | ○ | ○ | ○ | ○ |  | ○ | CH32固有 |
| TouchKey | 一部実装 | touchRead / touchSetCycles |  | ◐ | ◐ | ◐ | ◐ | ◐ |  | ◐ |  | ◐ |  | ◐ | arduino-esp32と同じ名前で生の数値。ADC1のTKey(CTLR1 bit24)、最短のサンプル時間とEXTTRIG切りで測る(EVTの長い設定のままでは4095か0)。何もつながない端子で範囲の半分前後になる充電時間を既定にした。V005〜V007/M007・X033/X035・V20x・V30x(X035・V006・V203・V307で値を確認、指で触った向きは未確認)。L103は同じ手順で読めず、V205・H41xは別のTKEYブロックで未実装、V103は未確認 |
| HSADC (高速ADC) | 一部実装 | CH32RVHsAdc(library) |  |  |  |  |  |  |  |  |  |  |  | ◐ | H415/H416/H417。DMAで連続取り込み、USBで流す(HsAdcStream) |
| DFSDM (ΣΔフィルタ) | 未実装 | - |  |  |  |  |  |  |  |  |  |  |  | ○ | PDMマイク、ΣΔ変調器の入力。H417 |
| PIOC (プログラマブルIO) | 未実装 | - |  |  |  |  | ○ |  |  |  |  | ○ |  | ○ | X035/V205固有。今はやらない(ユーザー、2026-11-11) |
| ADC 連続・DMA・トリガ | 一部実装 | CH32RVAdcStream(library) | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | 自走の連続変換をDMAの2面バッファで受ける(1〜6本、全系列)。タイマーのトリガ(`beginTimer()`: 系列のトリガ源の空いたタイマー、TRGOかコンペア。周期は正確で、自走の下限より遅くもできる)と端子の縁のトリガ(`beginExternal()`: 多くの系列でビット11の端子、X305/X315はビット12、V00xはPD3/PC2、M030はPA14/PB6)、`conversions()`。arduino-esp32の`analogContinuous()`(タイマーで回し、フレームごとの平均)もコアにある。2026-11-17、ビルドのみ。注入変換は未実装 |
| ADC アナログウォッチドッグ | 未実装 | - | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ |  |
| ADC デュアル | 未実装 | - |  |  |  | ○ |  | ○ | ○ |  |  |  | ○ | ○ | ADC1とADC2の同時・交互変換 |
| コンパレータ | 未実装 | - |  | ○ |  |  | ○ |  |  | ○ | ○ | ○ |  | ○ | OPAの中の比較器(CMP)。割込み・タイマーのブレーキ・起床に使える |

## USB

| ペリフェラル | 状態 | Arduino API | V003 | V00x/M007 | V103 | V203/V208 | V205 | V303/305/307/317 | V407/V467 | L103/M103 | M030 | X033/X035 | X305/X315 | H415/H416/H417 | 備考 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| USB (FS device/host) | 一部実装 | `CH32RVUsbDevice` / `CH32RVUsbHost` |  |  | ◐ | ◐ | ◐ | ◐ |  | ◐ | ◐ | ● |  | ◐ | stackは自前(TinyUSBは2026-10-20に撤去)。deviceはX035/X033とH417で実機確認、V103・V20x(USBFSとUSBD)・V30x・V205・L103/M103・M030はドライバがありビルドのみ(コントローラとドライバの一覧はdocs/usb.ja.md)。L103/M103・V20x/V30xはCPU ClockがUSBの作れる周波数のときだけ(メニューの項目に書いてある)。hostはX035(R8T6/C8T6/G8U6/G8R6、F8U6などはシリコンに無い)とUSBOTG_FSの系列(V20x・V30x・V205・L103/M103・M030・H41x)のUSBFS。X035 C8T6でESP32-S3相手に実機確認、ほかはビルドのみ。工場ISPへの切り替えはX033/X035(実機確認)とX305/X315(ビルドのみ)。ほかはBOOT_MODEが無いかH41xのように動かない |
| USB HS | 一部実装 | `CH32RVUsbDevice`(`CH32RVUsbController::HighSpeed`) |  |  |  |  | ◐ | ◐ | ◐ |  |  |  | ◐ | ◐ | 第1世代USBHS(V305/V307/V317、HSEが要る)と第2世代USBHS(V205・V407/V467・X305/X315・H41x)のdeviceドライバがあり、どれもビルドのみ。V303はUSBHSが無い。H41xのUSBHSはSWDと端子を共有する |
| USB SS | 一部実装 | `CH32RVUsbDevice`(`CH32RVUsbController::SuperSpeed`) |  |  |  |  |  |  |  |  |  |  | ◐ | ◐ | H417/H416のUSBSS device。H417で実機確認(DFU、vendor bulkで194〜246 MB/s)。X315のdeviceはビルドのみ。hostは未実装 |
| USB PD | 一部実装 | `CH32RVUsbPd`(library: `USBPD`、`USBPDSource`) |  |  |  |  | ◐ |  |  | ◐ | ◐ | ◐ | ◐ | ◐ | sinkとsource。X035のsinkは実機確認、L103/M103・V205・X305/X315・H41x・M030はビルドのみ(CCの端子と設定はlibraries/CH32RVUsbPd/README.ja.md)。X033はCCの端子が無い。M030はUSBPD0(PA0/PA1)だけ(比較器のビットが別。USBPD1は未対応) |

## 通信・外部バス

| ペリフェラル | 状態 | Arduino API | V003 | V00x/M007 | V103 | V203/V208 | V205 | V303/305/307/317 | V407/V467 | L103/M103 | M030 | X033/X035 | X305/X315 | H415/H416/H417 | 備考 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| CAN | 未実装 | - |  |  |  | ○ | ○ | ○ | ○ | ○ |  |  |  | ○ |  |
| Ethernet | 一部実装 | `ETH`(Network library) |  |  |  | ◐ |  | ◐ | ◐ |  |  |  |  | ◐ | H417の内蔵MAC+10/100 PHY(`NetworkEthernet.h`)。DHCP〜HTTPSまで実機確認(TCP 20/24 Mbit/s、docs/network-stack.ja.md)。H415/H416はETHが無い。V307・V407・V208も実機で通る(V208は2026-11-03、TCP 9.3/8.1 Mbit/s、TLSは無し)。V467・V317・V203RBはEVTから移してビルドのみ(V208/V203RBはHCLK 60/120 MHzが要り、CPU Clock `60MHz`を選ぶ。既定の96 MHzでは`begin()`がfalse) |
| I2S | 未実装 | - |  |  |  |  |  | ○ | ○ |  |  |  |  | ○ |  |
| SDIO | 一部実装 | SD_MMC |  |  |  |  |  | ◐ | ◐ |  |  |  |  | ◐ | SD_MMC library(4 bit、20 MHz、H417で実機確認)。DMA・high speed・MMCの4/8 bit・V4x7のremapは未実装 |
| FSMC/FMC | 未実装 | - |  |  |  |  | ○ | ○ | ○ |  |  |  |  | ○ | H417のFMCはSDRAMも扱う |
| SDMMC | 未実装 | - |  |  |  |  |  |  |  |  |  |  |  | ○ | H417。host(HS200/HS400)とSDIO slave |
| SAI (音声) | 未実装 | - |  |  |  |  |  |  |  |  |  |  |  | ○ | TDM、SPDIF出力 |
| SWPMI | 未実装 | - |  |  |  |  |  |  |  |  |  |  |  | ○ | NFC/UICCのSWP master |
| UHSIF (高速パラレル) | 一部実装 | CH32RVUhsif(library) |  |  |  |  |  |  |  |  |  |  |  | ◐ | 8/16/32 bitの入力・内部クロックだけ(スレーブのモード、出力、外部クロックは未対応)。レジスタは非公開で、実測から作った(docs/h417-uhsif.ja.md)。CH32RVLogicCaptureのbegin(UHSIF_DATA)が使う |
| SerDes | 未実装 | - |  |  |  |  |  |  |  |  |  |  |  | ○ | 1.5 Gbpsの差動リンク |
| QSPI | 未実装 | - |  |  |  |  | ○ |  |  |  |  |  |  | ○ |  |
| PSRAM | 未実装 | - |  |  |  |  |  |  | ○ |  |  |  |  |  |  |
| I3C | 未実装 | - |  |  |  |  |  |  | ○ |  |  |  |  | ○ |  |
| DVP (カメラ) | 未実装 | - |  |  |  |  |  | ○ | ○ |  |  |  |  | ○ | パラレル入力(8〜12 bit、外部PCLK)の候補。docs/unused-peripherals.ja.md |
| LTDC (LCD) | 未実装 | - |  |  |  |  |  |  | ○ |  |  |  |  | ○ |  |
| GPHA (2D DMA) | 未実装 | - |  |  |  |  |  |  |  |  |  |  |  | ○ |  |
| ARGB (LEDドライバ) | 実装済 | `rgbLedWrite` / `ch32rv_rgb_led_write` |  |  |  |  |  |  | ● |  |  |  | ● |  | ARGBの端子(V4x7はPA15、X3x5はPA15・PD3・PD4)では同じ関数がブロックとDMAで送り、割り込みを止めない(2026-11-17、ビルドのみ)。ほかの端子と系列はソフト(arduino-esp32の名前、どの端子でも、全系列、2026-11-17) |
| RNG | 実装済 | `ch32rv_random` |  |  |  |  |  | ● | ● |  |  |  |  | ● | `ch32rv_random()`・`ch32rv_fill_random()`(arduino-esp32の`esp_random()`・`esp_fill_random()`)。TLS(mbedTLS)とTCPの乱数にも使う(`ch32rv_rng_read`)。回路の無い系列ではUIDと時刻を混ぜたソフトの値(暗号には使えない)。`random()`はArduinoのとおり再現できる疑似乱数のまま(`randomSeed(ch32rv_random())`)。selfapiの試験はビルドのみ(2026-11-17) |
| BLE | 除外 | - |  |  |  | − |  |  |  |  |  |  |  |  | V208のみ。ユーザーの判断で作らない(2026-11-14) |
| LVDS | 未実装 | - |  |  |  |  | ○ |  |  |  |  |  |  |  | V205。用途は未確認(ch32-device-dataに問い合わせ中) |

## デバッグ出力

| ペリフェラル | 状態 | Arduino API | V003 | V00x/M007 | V103 | V203/V208 | V205 | V303/305/307/317 | V407/V467 | L103/M103 | M030 | X033/X035 | X305/X315 | H415/H416/H417 | 備考 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| SDI print (debug出力) | 実装済 | SerialSDI(library) | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | 送信のみ |
| RTT (debug出力) | 実装済 | SerialRTT(library) | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | 双方向。RAM ring bufferを使用 |
| DMDATA mailbox (debug出力) | 実装済 | SerialDMDATA(library) | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | 双方向。SerialSDIと同時使用不可 |
| DMSEQ console (debug出力) | 実装済 | SerialDMSeq(library) | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | 双方向。通し番号とCRC-8を使用 |

## RTOS

| ペリフェラル | 状態 | Arduino API | V003 | V00x/M007 | V103 | V203/V208 | V205 | V303/305/307/317 | V407/V467 | L103/M103 | M030 | X033/X035 | X305/X315 | H415/H416/H417 | 備考 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| FreeRTOS | 未実装 | - |  |  | ○ | ○ | ○ | ○ |  | ○ | ○ | ○ |  |  | coreはbare metal(ADR-0006)。portはcoreと独立したlibraryとして作る |
| RT-Thread | 未実装 | - |  |  | ○ | ○ | ○ | ○ |  | ○ | ○ | ○ |  |  | coreはbare metal(ADR-0006)。portはcoreと独立したlibraryとして作る |
| HarmonyOS | 未実装 | - |  |  | ○ | ○ | ○ | ○ |  | ○ | ○ | ○ |  |  | coreはbare metal(ADR-0006)。portはcoreと独立したlibraryとして作る |
| TencentOS | 未実装 | - |  |  | ○ | ○ | ○ | ○ |  | ○ | ○ | ○ |  |  | coreはbare metal(ADR-0006)。portはcoreと独立したlibraryとして作る |

## この表の使いかた

- 状態は repository 内の実装範囲を表します。
- series列が空欄のペリフェラルは、そのfamilyの silicon に無いかEVTに例が無いかのどちらかです。実装するときは`ch32-device-data`側でも裏を取ってください。
