# 未実装の機能（2026-11-14）

機能は基本すべて作ります。作らないのは特殊なもの（今は BLE だけ）で、それはユーザーが決めます（2026-11-14）。
実装が無いこと、実機や相手が無いことは、外す理由にしません。確かめる機材が無いものも先に実装し、試験（sketch と pytest）と
配線の案を用意して「配線待ち」にします（[tests/TEST_PLAN.ja.md](../tests/TEST_PLAN.ja.md) の「試験の段階と役割」）。

この一覧は、対応表・各 README・FEATURE_CANDIDATES・ADR・TODO・coverage・EVT の例とレジスタの一覧を突き合わせた棚卸しです
（行ごとの根拠は棚卸しの元の表にあり、主なものを「根拠」に残しました）。「対象外」「範囲外」「不要そう」と書かれていたものも
未実装として数えています。

- **決定済み・保留**: ユーザーが過去に「作らない」「今はやらない」と決めたものは、その決定のまま（優先の欄に書く）。作る対象には
  しない（2026-11-17、ユーザー: 実装しないことにしたはず）。
- **優先**（案。ユーザーと決める）: 1 = Arduino の利用者がよく使う API と、今の実装の穴（系列の抜け、ライブラリの片側だけ）。
  2 = ほかのペリフェラルと機能。3 = 範囲そのものの判断が要るもの（RTOS の port、互換層、別アーキテクチャなど）。
  未実装はどれも、実装済みの機能の磨き込みより先に置きます。
- **試験**: 自動 = 常駐ベンチか CI で回せる（DUT の自己検査、OEP プローブ、PC・Pico・ESP32・CH32RV どうしの対向）。
  単発 = 計測器や専用の機材を人が用意して測る（日付と条件を文書に残す）。単発→自動 = 立ち上げは単発、通ったら自動にする。

## クロック・電源・システム

| 機能 | 系列 | 優先 | 試験 | 要るもの | 根拠 |
|---|---|---|---|---|---|
| HSE（外付け水晶）の残りの系列 | M030、X315（X305）、V407/V467 | 1 | 単発→自動 | 水晶の載った板（EVT 板）。自己（selfcheck-clocks で RCC を読む＋時間の速さ） | PS:62, PM:294 |
| HSE の周波数の選択（EVT の前提以外の水晶） | HSE のある全系列 | 2 | 単発→自動 | 別の周波数の水晶の板。自己 | README.ja.md:49 |
| HSI の外での MCO（クロック出力） | 全 12 family | 2 | 自動 | プローブ 1 本（周波数を測る） | PS:61, PM:292 |
| 実行中のクロック変更 | 全系列 | 決定済み: 作らない（SYS-05） | — | 自己（変更前後で millis・Serial・PWM の selfcheck） | PS:40, FC:33 |
| H41x・X305 の CPU Clock メニュー | H415/H416/H417、X305 | 1 | 自動 | 自己（selfcheck） | PS:40, TODO:504 |
| HSI / LSI の校正（トリム）API | V103、V20x、V205、V30x、V407、L103、M030、H417 | 2 | 自動 | 自己（HSE か RTC と比べる）、またはプローブで周波数 | EVT RCC/HSI_Calibration, RCC/LSI_Calibration |
| LSE（32 kHz 水晶） | RTC のある系列 | 2 | 単発→自動 | LSE の載った板。自己 | PS:53 |
| PVD（電圧低下の検出・割込み・起床） | V003〜H417 の 11 family | 2 | 単発 | 可変電源（VDD を下げる）。ベンチでは電源を動かせないので手作業 | PS:60, PM:290 |
| 休止（lightSleep / deepSleep）の残りの系列 | V103、M030、X315（X305）、V407/V467 | 実装済み（2026-11-15、ビルドのみ。V103・V4x7・X3x5 は RTC のアラーム、M030 は AWU。V103 は以前最初の休止から起きなかった件を実機で最初に確かめる） | 自動 | プローブ（端子で起こす・時間を測る）。system::test_sleep、selfsys::test_light_sleep。V103 の復旧手段（uhubctl） | libraries/CH32RV/README.ja.md |
| 周辺を動かしたままの休止 | 全系列 | 2 | 自動 | プローブ | FC:31 |
| Standby の RAM 保持・低電圧の形 | V20x、V205、V30x、V407、L103 | 2 | 単発→自動 | プローブ（起床）＋電流計 | PS:41, EVT PWR/Standby_RAM_Mode・Standby_RAM_LV_Mode・Standby_RAM_Application |
| 停止モード 3（LPDS=1、FLASH_LP） | L103/M103、V205 | 2 | 単発→自動 | 電流計、プローブ | TODO:191 |
| VDD12 の外部給電・低消費の Stop | H417、X315 | 2 | 単発 | 外部 1.2 V 電源の板（手作業） | EVT PWR/VDD12_ExternPower・VDD12_ExternPowerInStopLP |
| 過電圧保護（OVP）の設定 | M030 | 2 | 単発 | 可変電源（壊す試験に近い。専用チップ） | docs/adr/0017-option-byte-menus.ja.md:23, EVT CH32M030 PWR/PWR_OVP |
| RTC の残りの系列 | H41x、V4x7、X315、V103 | 実装済み（H41x は 2026-11-14。V4x7・X3x5・V103 は 2026-11-15、ビルドのみ） | 自動 | 自己（selfsys::test_rtc） | PS:53 |
| RTC のカレンダー・アラームの公開 API | RTC の系列 | 実装済み（2026-11-15、ビルドのみ。暦は POSIX、`CH32RV.setAlarm()`・`enableAlarmWakeup()`） | 自動 | 自己＋プローブ。system::test_rtc_alarm、selfsys::test_rtc_alarm | libraries/CH32RV/README.ja.md |
| RTC の校正・校正クロック出力 | V103、V20x、V205、V30x、V407、H417、X315 | 2 | 自動 | プローブ（1 Hz / 512 Hz を測る） | EVT RTC/RTC_Calibrations, RTC/RTC_OutputCalibrateClock（V205） |
| BKP の公開 API | RTC の系列 | 実装済み（2026-11-15、ビルドのみ。`CH32RV.get/setBackupRegister()`。H41x・X3x5 は .noinit の RAM） | 自動 | 自己（リセットをまたいで読む）。system::test_backup_registers、selfsys::test_backup_registers | libraries/CH32RV/README.ja.md |
| WWDG | 全 12 family | 実装済み（2026-11-15、ビルドのみ。`CH32RV.wwdgEnable()`） | 自動 | 自己（窓の外で餌→リセット理由）とプローブ（餌からリセットまで）。system::test_window_watchdog、selfsys::test_window_watchdog | libraries/CH32RV/README.ja.md |
| IWDG のオプションバイト（Watchdog メニュー）の残り | V103、H41x | 2 | 自動 | 自己（書き換えと戻し） | docs/adr/0017-option-byte-menus.ja.md:21 |
| PFIC の割込み優先度の設定 | 全系列 | 1 | 自動 | 自己（タイマー 2 本で入れ子を確かめる） | PS:38, FC:37 |
| VTF（高速割込み）、割込みの入れ子、ベクタを RAM へ | 全系列 | 2 | 自動 | 自己（遅延を SysTick で測る） | EVT INT/Interrupt_VTF, INT/VectorInRAM |
| オプションバイト（読み出し保護・書き込み保護・DATA0/1・START_MODE・STANDYRST/STOPRST・USARTDLEN 等・PORCTR・RST_PIN_SEL・CFGCANM） | 全 12 family | 2 | 自動 | 自己（読み戻し）。保護は解くとフラッシュが消えるので専用チップ | PS:69, PM:308 |
| PMP（メモリ保護） | V20x、V30x、V4x7、L103、X035、H41x | 2 | 自動 | 自己（禁止域に触れて例外） | PS:63, PM:296 |
| フラッシュの ECC の診断 | M030 | 2 | 自動 | 自己 | EVT CH32M030 FLASH/FLASH_ECC, TODO:318 |
| BootAsUser（BOOT 域をユーザーに） | V003、V006、X035 | 2 | 自動 | 自己 | EVT FLASH/BootAsUser |
| ADC・I2C のクロック範囲（バスを分周できない系列） | X315、H417、M030 | 2 | 自動 | プローブ（I2C の SCL） | TODO:190 |

## GPIO・割込み

| 機能 | 系列 | 優先 | 試験 | 要るもの | 根拠 |
|---|---|---|---|---|---|
| GPIO 以外の EXTI の線（PVD・USB 起床など。RTC のアラームは `CH32RV.setAlarm()` が使う） | 全系列 | 1 | 単発→自動 | 自己（RTC アラームの線）、PVD は可変電源 | PS:37, FC:36 |
| EXTI のイベントモード（割込みでなく起床イベント） | 全系列 | 2 | 自動 | プローブ | PS:37 |
| GPIO の速さ・駆動力の設定 | 全系列 | 2 | 自動 | プローブ（立ち上がり時間。速い取り込み） | EVT GPIO/GPIO_Toggle |
| PWM の remap 先・CHxN の端子 | TIM のある全系列 | 1 | 自動 | プローブ | PS:48, FC:70 |
| TIM8〜10 の PWM の実機 | V30x、V4x7、H41x | 2 | 自動 | プローブ | PS:48 |
| 既定以外の route の実機（X305/X315 の Wire など） | X305/X315 ほか | 2 | 自動 | プローブ＋プルアップ | TODO:498 |
| SD_MMC の V4x7 の remap、H41x の AF の端子 | V4x7、H41x | 2 | 単発 | SD カードの口のある板 | libraries/SD_MMC/README.ja.md:52 |

## タイマー・PWM

| 機能 | 系列 | 優先 | 試験 | 要るもの | 根拠 |
|---|---|---|---|---|---|
| 相補PWM（CHxN）・デッドタイム・ブレーキ | 全 12 family | 2 | 自動 | プローブ 2〜3 本（CHx と CHxN の重なり）。ブレーキはプローブが BKIN を動かす | PS:59, PM:280 |
| 非対称デッドタイム | M030、H417 | 2 | 自動 | プローブ | UNUSED:85, EVT TIM/Dead_Asymmetry |
| ワンパルス | 全系列 | 2 | 自動 | プローブ（トリガーを入れて幅を測る） | PS:59, EVT TIM/One_Pulse |
| 出力比較（Output Compare） | 全系列 | 2 | 自動 | プローブ | PS:59, EVT TIM/Output_Compare_Mode |
| 6 ステップ PWM、位相シフト PWM、中央揃え、独立出力 | V003、V103、V20x、V205、V30x、V407、M030 | 2 | 自動 | プローブ | EVT TIM/PWM_6_Step, PWM_Phase-shifted |
| タイマーの同期・連結（マスター／スレーブ、外部トリガで開始） | 全系列 | 2 | 自動 | プローブ | UNUSED:84, TODO:301 |
| ETR（外部クロック）・外部パルスの計数 | 全系列 | 2 | 自動 | プローブ（クロックを入れる） | PS:48, FC:69 |
| タイマーの DMA バースト | 全系列 | 2 | 自動 | プローブ | PS:48, EVT TIM/TIM_DMA |
| 入力捕捉: エッジごとの時刻・通知 | 全系列 | 1 | 自動 | プローブ | PS:57, FC:68 |
| 両エッジの捕捉・CAPLVL（捕捉時のレベル） | M030、H417、V003、V00x、X035、V205、L103、X315 | 2 | 自動 | プローブ | UNUSED:42,85, EVT TIM/Bothedge_Capture |
| 入力捕捉・エンコーダの CH1/CH2 以外の端子 | 全系列 | 2 | 自動 | プローブ | PS:57-58, FC:69 |
| `pulseIn()` のハード化 | 全系列 | 1 | 自動 | プローブ | PS:57 |
| 利用者向けタイマー API（周期・単発・時間単位） | 全系列 | 1 | 自動 | 自己 | FC:66 |
| Ticker 相当 | 全系列 | 1 | 自動 | 自己 | FC:67 |
| ADC をタイマーで捕捉（TIM/ADC_Capture） | M030 | 2 | 自動 | プローブ | EVT CH32M030 TIM/ADC_Capture |
| 32 bit の TIM4 | V205、X315、L103 | 2 | 自動 | 自己 | UNUSED:85 |
| LPTIM（休止中の計時・起床、PWM・ワンパルス） | L103、H417 | 2 | 自動 | プローブ（起床・波形） | PS:49, PM:200 |
| SLTIM（V006 の小さいタイマー）と ADC トリガ | V006（V00x） | 2 | 自動 | プローブ | EVT CH32V006 TIM/SLTIM_DMA, ADC/SLTIM_Trigger |
| SoftPWM | 全系列 | 2 | 自動 | プローブ | docs/software-peripherals.ja.md:20, FC:73 |

## アナログ

| 機能 | 系列 | 優先 | 試験 | 要るもの | 根拠 |
|---|---|---|---|---|---|
| ADC のタイマー・外部トリガ | 全系列 | 1 | 自動 | プローブ（方形波を入れて間隔を見る） | PS:80, PM:282 |
| ADC の注入変換・不連続モード | 全系列 | 2 | 自動 | プローブ | PS:80, EVT ADC/Auto_Injection・Discontinuous_mode |
| ADC アナログウォッチドッグ | 全 12 family | 2 | 自動 | プローブ（電圧を動かす）。自己なら DAC の系列 | PS:81, PM:284 |
| ADC デュアル（同時・交互） | V20x、V30x、V4x7、X3x5、H41x | 2 | 自動 | プローブ 2 本 | PS:82, PM:286 |
| ADC の PGA（入力増幅） | L103、V205、H417 | 2 | 単発 | 外部の電圧源（精度はマニュアル試験） | UNUSED:74, EVT ADC/ADC_PGA |
| ADC の平均（ADC_AVG）・高速変換 | M030、L103 | 2 | 自動 | プローブ | EVT CH32M030 ADC/ADC_AVG, CH32L103 ADC/ADC_FastConvent |
| X315 の ADC の縦続・走査出力・注入の交互 | X305/X315 | 2 | 自動 | プローブ | EVT CH32X315 ADC/ADC_Cascade・ADC_ScanOut・ADC_InjeAlter |
| 外付け CH448 で 384 チャネル | X305/X315 | 2 | 単発 | CH448 の板 | UNUSED:75 |
| mV を返す `analogReadMilliVolts()` | 全系列 | 1 | 単発 | 外部の電圧源（マニュアル） | PS:46, FC:76 |
| ADC の工場校正値（Get_CalibrationValue） | V20x、V30x | 2 | 単発 | 外部の電圧計（マニュアル） | TODO:499 |
| 外部の温度チャネル | V103、V20x、V30x | 2 | 単発 | 温度センサの板 | EVT ADC/Temperature_External_channel |
| DAC の DMA 波形・タイマートリガ・三角波・ノイズ・2 ch 同時 | V30x、V4x7、H41x | 2 | 自動 | プローブ（ADC の入力）。自己なら ADC で読み戻す | UNUSED:76, FC:77 |
| OPA（内蔵アンプ・PGA・電圧フォロワ） | V003、V00x、V20x、V205、V30x、V4x7、L103、M030、X035、H41x | 2 | 自動 | プローブ（入力電圧）＋ADC で自己読み | PS:75, PM:212 |
| コンパレータ（CMP、割込み・ブレーキ・起床・TIM 入力） | V00x、V205、L103、M030、X035、H41x | 2 | 自動 | プローブ | PS:83, PM:288 |
| TouchKey の残り | L103、V205、H41x、V103 | 2 | 単発→自動 | 自己（何もつながない端子の値）。指の向きは手作業 | PS:76, EVT TKEY/TKEY（V205 |
| TouchKey のしきい値・イベント | TouchKey の系列 | 2 | 単発→自動 | 手作業（指）か、プローブで容量を切り替える治具 | FC:79 |
| HSADC の残り | H41x | 2 | 自動 | プローブ（常駐ベンチに線が無い、TODO:148） | PS:77 |
| DFSDM（ΣΔ フィルタ、PDM マイク） | H417 | 2 | 単発→自動 | PDM マイクか ΣΔ 変調器、またはプローブが PDM を出す | PS:78, PM:218 |
| M030 の QII・ISP・ISINK・ISOURCE（電流検出・DC-DC 調整） | M030 | 2 | 単発 | モーター基板（M030 は端子を出力にしない約束） | UNUSED:79,98 |

## シリアルバス（UART・I2C・SPI・CAN・I2S など）

| 機能 | 系列 | 優先 | 試験 | 要るもの | 根拠 |
|---|---|---|---|---|---|
| UART のハードウェアフロー制御（RTS/CTS） | 全系列（CTS/RTS のある USART） | 実装済み（2026-11-17、`setPins(rx, tx, cts, rts)`・`setHwFlowCtrlMode()`。ベンチ未確認） | 自動 | プローブ（CTS を動かす、RTS を取り込む）。uart::test_uart_flow_control | cores/arduino/HardwareSerialMode.cpp, FC:52 |
| RS485 の方向制御（DE/RE） | 全系列 | 実装済み（2026-11-17、`setMode(UART_MODE_RS485_HALF_DUPLEX)`、DE はコアが TC の割込みで動かす。ベンチ未確認） | 自動 | プローブ（DE の線と TX の重なり）。uart::test_uart_rs485。トランシーバーを挟んだ periph の rs485 は別 | cores/arduino/HardwareSerialMode.cpp, FC:50, COV:242-245（periph::test_planned[rs485]） |
| UART 1 線半二重 | 全系列 | 実装済み（2026-11-17、`setPins(p, p)`。X033/X035 は開放ドレインが無く受信は未確認。ベンチ未確認） | 自動 | プローブ（プルアップ）。uart::test_uart_half_duplex | cores/arduino/HardwareSerialMode.cpp, FC:51, EVT USART/USART_HalfDuplex |
| UART 受信イベント・エラー通知・IDLE | 全系列 | 実装済み（2026-11-17、`onReceive()`・`setRxTimeout()`・`setRxFIFOFull()`、`onReceiveError()` にノイズ。ベンチ未確認） | 自動 | プローブ。uart::test_uart_receive_event | cores/arduino/HardwareSerialMode.cpp, FC:53 |
| UART の DMA 受信・送信 | 全系列 | 1 | 自動 | プローブ | TODO:303, EVT USART/USART_DMA |
| UART の IrDA（`setMode(UART_MODE_IRDA)`、今は false） | 全系列 | 2 | 自動 | IrDA のパルスを読める相手（プローブで取り込み） | EVT の USART に例なし、RM の IREN |
| UART 同期モード（クロック出力）・スマートカード・マルチプロセッサ（アドレス起床） | 全系列（スマートカードは V20x、V205、L103、X035） | 2 | 自動 | プローブ（同期は SPI として取り込む） | EVT USART/USART_SynchronousMode・USART_SmartCard・USART_MultiProcessorCommunication |
| SoftwareSerial | 全系列 | 実装済み（2026-11-17、ライブラリ。ベンチ未確認） | 自動 | プローブ。uart::test_uart_software_serial | libraries/SoftwareSerial, FC:54 |
| I2C マルチマスター | 全系列 | 1 | 自動 | プローブ 2 本でもう 1 つのマスター | FC:47, libraries/Wire/src/Wire.h:22 |
| I2C のマスターとスレーブの兼用 | 全系列 | 1 | 自動 | プローブ（i2c-controller と i2c-target） | FC:48, libraries/Wire/src/Wire.h:21 |
| I2C の 10 bit アドレス・General Call | 全系列 | 1 | 自動 | プローブ（i2c-target / controller が 10 bit を扱えるかは [oep] に確認） | FC:49, libraries/Wire/src/Wire.h:22-23 |
| I2C の PEC（SMBus）・DMA | 全系列 | 2 | 自動 | プローブ | TODO:307, EVT I2C/I2C_PEC・I2C_DMA |
| Wire の NACK・timeout・回復・clock stretch の試験 | 全系列 | 1 | 自動 | プローブ（SDA を握る相手） | TODO:27, TC:81 |
| SoftWire のスレーブ | 全系列 | 実装済み（2026-11-17、7 台で確認。100 kHz 台まで） | 自動 | プローブ（i2c-controller）。softwire::test_wire_slave | libraries/Wire/README.ja.md, TC:87 |
| SPI ライブラリのスレーブ（`SPI_HAS_PERIPHERAL_MODE`） | 全系列 | 実装済み（2026-11-15、SPI と SoftSPI。V203・V006・V205・V103 で確認） | 自動 | プローブ（spi-controller）。spi・softspi::test_spi_peripheral | libraries/SPI/README.ja.md |
| CH32RVSPISlave の残り（DMA、ハードの NSS、上限クロック、V407・H417） | 全系列 | 2 | 自動 | プローブ（速いマスター） | PS:45, libraries/CH32RVSPISlave/README.ja.md:38,48 |
| SoftSPI のスレーブ | 全系列 | 実装済み（2026-11-15、SoftSPISlave。V203・V006・V205・V103 で確認） | 自動 | プローブ（spi-controller）。softspi::test_spi_slave | libraries/SPI/README.ja.md, TC:90 |
| SPI のハードウェア CS（NSS） | 全系列 | 2 | 自動 | プローブ | FC:45, EVT SPI/FullDuplex_HardNSS |
| SPI の半二重・片方向（3 線） | 全系列 | 2 | 自動 | プローブ | FC:46, EVT SPI/1Lines_half-duplex |
| SPI の非同期・DMA 転送 | 全系列 | 2 | 自動 | プローブ | FC:44, EVT SPI/SPI_DMA・SPI_DMA_EXT |
| SPI の CRC | V003、V00x、V20x、V205、V30x、V4x7、L103、M030、X035 | 2 | 自動 | プローブ | EVT SPI/SPI_CRC |
| CAN 2.0B（HardwareCAN、フィルタ、ループバック、Listen-only、バスオフ） | V20x、V205、V30x、V4x7、L103/M103、H41x（3 個、TTCAN） | 2 | 自動 | 1 台: 内部ループバックとサイレントで自己。2 台目かプローブの CAN、トランシーバーと終端 | PS:98, PM:236 |
| CAN の時間トリガ（TTCAN）・CAN 起床 | 上と同じ（起床は L103） | 2 | 単発 | 同上 | EVT CAN/Time-triggered, CAN/CAN_Wakeup |
| CAN FD | L103（C8T6、K8U6） | 2 | 単発 | FD 対応トランシーバー、相手 | FC:55, UNUSED:55 |
| CFGCANM（CAN のオプションバイト） | L103、V205 | 2 | 自動 | 自己 | docs/adr/0017-option-byte-menus.ja.md:26 |
| I2S | V30x、V4x7、H41x | 2 | 自動 | プローブ（BCLK・WS・SD を取り込む）。受信はプローブが I2S を出す | PS:100, PM:243 |
| SAI（TDM、SPDIF 出力） | H417 | 2 | 自動 | プローブ（取り込み） | PS:104, PM:247 |
| I3C（controller・target、IBI、Hot-Join） | V4x7、H41x | 2 | 自動 | I3C の相手（2 台目の H417 か V407）、プローブで取り込み | PS:110, PM:255 |
| SWPMI（SWP master） | H417 | 2 | 自動 | SWP のカードか 2 台目 | PS:105, PM:248 |
| QSPI（Flash、メモリマップ、dual-flash） | V205、H41x（V203CCT6 も） | 2 | 単発→自動 | QSPI Flash の板（EVT 板）。プローブで取り込み | PS:108, PM:253 |
| ARGB（WS2812 のハードウェア） | V4x7、X3x5 | 1 | 自動 | プローブ（波形）。LED 1 個 | PS:114, PM:259 |

## USB デバイス

| 機能 | 系列 | 優先 | 試験 | 要るもの | 根拠 |
|---|---|---|---|---|---|
| ファームウェア更新のクラス（DFU の download、MSC の UF2 = MscFirmwareDisk） | 全 USB 系列 | 1 | 自動 | PC が相手（usb の case）。自動 | docs/usb.ja.md:376, libraries/CH32RVUsbDevice/README.ja.md:76 |
| ソフトウェア USB デバイス（rv003usb） | V003（V00x） | 2 | 自動 | USB 端子の配線（D+/D- を GPIO に）＋PC | FC:116, TODO:479 |
| 工場 ISP への入口（enterBootloader） | H41x | 2 | 自動 | PC（ch32rv isp） | docs/usb.ja.md:371, FC:115 |
| 工場 ISP への入口（BOOT_MODE の無い系列） | V20x、V30x、V103、L103、V205、M030、V4x7 | 2 | 自動 | PC | docs/usb.ja.md:372-373, docs/upload-routes.ja.md:174 |
| USBSS のバルクストリーム（UAS。デバイスもホストも） | H417/H416、X315 | ハードの限界（2026-11-14 の実験） | — | デバイスが MaxStreams 2 を名乗ると PC の xHCI はストリームを割り当てるが、ストリーム 1 の転送は OUT・IN とも時間切れで、カーネルは「Transfer event for unknown stream ring」。RM 27 章と EVT のヘッダーに Stream ID の欄が無い。ホストの側にも欄が無い（HCD の下書きで確認） | TODO:136-137 |
| USBSS デバイスの等時の Mult>0（1 間隔に複数 burst） | H417/H416、X315 | 1 | 自動 | PC（libusb の等時）か H417 どうし | TODO |
| USBSS の U1/U2/U3 | H417/H416、X315 | 2 | 自動 | PC（Linux の LPM） | libraries/CH32RVUsbDevice/src/CH32RVUsbDcdUsbss.cpp:26,207,263 |
| V20x の USBD と USBFS を同時に使う（2 つのデバイス） | V203/V208 | 2 | 自動 | PC の口 2 つ | libraries/CH32RVUsbDevice/src/CH32RVUsbDevice.h:44-45 |
| V407 の 2 つのデバイス・デバイスとホストの同時 | V407/V467 | 2 | 自動 | PC＋周辺機器 | docs/usb.ja.md:347, EVT CH32V407 USBHS/Dual_device・Device_Host |
| USBSS の口の USB 2.0 側（同じコネクタの HS への切り替え） | H417/H416 | 2 | 自動 | PC、SWD を外す部品 | libraries/CH32RVUsbDevice/src/CH32RVUsbDcdUsbss.cpp:29-33, TODO:135-136 |
| EspUsbDevice との差（複数リスナー、キュー、64 bit LBA、CCID の追加 API、Info の例、例 30 本ほど） | 全 USB 系列 | 2 | 自動 | PC、ESP32-S3 | TODO:468 |
| USB デバイスのコールバックが関数ポインタ 1 枠（`std::function`・複数登録なし） | 全 | 2 | 自動 | 自己 | libraries/CH32RVUsbHost/README.ja.md:94 |
| suspend / resume・リモートウェイクアップの試験 | 全 USB 系列 | 1 | 自動 | PC（suspend を起こす） | libraries/CH32RVUsbDevice/src/CH32RVUsbDevice.h:123-125, TC:139 |

## USB ホスト

| 機能 | 系列 | 優先 | 試験 | 要るもの | 根拠 |
|---|---|---|---|---|---|
| V103 の USB（旧 IP）のホスト | V103 | 1 | 自動 | Pico か ESP32-S3（usb-peer-host） | COV:179-188,196,204,212, TODO:473 |
| 第 1 世代 USBHS のホスト | V305/V307/V317 | 1 | 単発→自動 | ESP32-P4（HS のデバイス）、HSE の板 | libraries/CH32RVUsbHost/src/CH32RVUsbHostPort.h:17, libraries/CH32RVUsbHost/README.ja.md:47-48 |
| USBSS のホスト（全部: リンク・制御・バルク・ERDY・burst・割込み・等時・SS ハブ・warm reset、ストリーム、Mult>0、U1〜U3） | H417/H416（X315 はシリコンがデバイスのみ） | 1 | 単発→自動 | H417 どうしの直結（片方 DCD）、USB 3.0 メモリ（手作業） | docs/usb.ja.md:367, PS:91 |
| HS ハブの先の FS・LS（split トランザクション） | 第 2 世代 USBHS のホスト（V205、V4x7、X3x5、H41x） | 1 | 自動 | HS ハブ＋FS の Pico | libraries/CH32RVUsbHost/README.ja.md:66, docs/usb.ja.md:344 |
| ハブ越しの LS 機器（PRE PID）の確認 | USBFS のホスト | 2 | 単発 | FS ハブ＋LS の機器 | TODO:472-473 |
| ホストのアイソクロナス（オーディオ・ビデオ） | 全ホスト | 1 | 自動 | ESP32-S3 / P4 の UAC・UVC デバイス | libraries/CH32RVUsbHost/README.ja.md:99, libraries/CH32RVUsbHost/src/CH32RVUsbHost.h:31 |
| ホストの USB ネットワーク（NCM / ECM） | 全ホスト | 1 | 自動 | ESP32-S3 の NCM か USB の LAN アダプタ | FC:113, TODO:472 |
| ホストの MSC のファイルシステム（マウント） | 全ホスト | 1 | 自動 | libraries/CH32RVUsbHost/README.ja.md:100、libraries/FS/README.ja.md:38、docs/filesystem.ja.md:286、FC:93 | まだ無い』 |
| ホストの USB シリアルの `setRxBufferSize()` | 全ホスト | 2 | 自動 | Pico | libraries/CH32RVUsbHost/README.ja.md:102 |
| H41x の USBHS ホスト（SWD と兼用、口の 2.0 側） | H41x | 1 | 自動 | ESP32-P4、SWD を外す部品 | docs/usb.ja.md:341, TODO:457 |
| ホストの Audio / Video / Network クラス（EspUsbHost との差） | 全ホスト | 2 | 自動 | ESP32-S3 / P4 | TODO:468 |

## USB PD

| 機能 | 系列 | 優先 | 試験 | 要るもの | 根拠 |
|---|---|---|---|---|---|
| M030 の USBPD1（PA2/PA3） | M030 | 2 | 単発→自動 | PD の相手（2 台目の CH32RV か充電器）。M030 はモーター基板（端子を出力にしない） | PS:92, libraries/CH32RVUsbPd/README.ja.md:47 |
| DRP・Power Role / Data Role / VCONN Swap | PD の全系列 | 2 | 自動 | 2 台の CC どうしの配線（TODO:150） | FC:118 |
| EPR / AVS、Battery / Variable の要求 | PD の全系列 | 2 | 単発→自動 | EPR の充電器（手作業）、2 台 | FC:119 |
| 外付け CH211（I2C の PD） | X035、L103 | 2 | 単発 | CH211 の板 | docs/wch-evt-binary-libraries.ja.md:32, EVT USBPD/USBPD_CH211 |
| 実装していない PD メッセージ（Not_Supported / Reject で答えるもの） | PD の全系列 | 2 | 自動 | 2 台 | libraries/CH32RVUsbPd/README.ja.md:148 |
| 実際の電圧の出力（ソースの VBUS の切り替え） | ソース | 2 | 単発 | 電源の切り替えのある板 | TODO:492 |

## ネットワーク

| 機能 | 系列 | 優先 | 試験 | 要るもの | 根拠 |
|---|---|---|---|---|---|
| 外付け PHY（100M/1G、RMII/RGMII）・ギガビット MAC | V307/V317、H417 | 2 | 単発 | 外付け PHY の板、スイッチ | docs/network-stack.ja.md:12, UNUSED:51-52 |
| V407 の 2 ポート（内蔵 MAC と CH390D） | V407/V467 | 2 | 単発 | CH390D の板 | docs/wch-evt-binary-libraries.ja.md:26, EVT CH32V407 ETH/DualNetPorts |
| 外付け SPI Ethernet | 全系列 | 2 | 単発 | W5500 などの板 | FC:94 |
| MAC の無い系列の IP（USB NCM の上） | X035 ほか | 2 | 自動 | PC（NCM） | docs/network-stack.ja.md:15,18 |
| V208 の TLS | V208 | 2 | 自動 | スイッチ＋PC | PS:99, libraries/Network/README.ja.md:8 |
| HTTP クライアント・サーバー、MQTT | Network の系列 | 2 | 自動 | PC（eth の case） | FC:96, TODO:247,464 |
| mDNS の問い合わせ（`queryHost()`） | Network の系列 | 2 | 自動 | PC | libraries/Network/README.ja.md:67 |
| PTP（IEEE 1588）・MDIX の設定 | V317、V4x7、H417 | 2 | 単発 | PTP の相手 | UNUSED:51 |
| IoCHub | H417、V407、V307 | 3（範囲の判断） | — | — | docs/wch-evt-binary-libraries.ja.md:27 |
| ETH の IAP（ネットワーク越しの書き換え） | V20x、V307、V407、H417 | 2 | 自動 | PC | EVT ETH/ETH_IAP |
| V307 のリビジョン 0x20 の回避、V208 の USB と Ethernet の同時 | V307、V208 | 2 | 単発 | スイッチ | TODO:464, docs/usb.ja.md:364-365 |
| BLE・無線（V208 の BLE、BLE Mesh、LWNS） | V208 | 除外 | — | （除外） | PS:116, PM:262 |

## ストレージ・メモリ

| 機能 | 系列 | 優先 | 試験 | 要るもの | 根拠 |
|---|---|---|---|---|---|
| SDIO（SD_MMC）の残り: DMA、high speed、ハードのフロー制御、H41x の AF・V4x7 の remap、MMC 4/8 bit | V30x、V4x7、H41x | 1 | 自動 | SD カードの口の板（V307 評価板） | libraries/SD_MMC/README.ja.md:40,50-53, PS:101 |
| SDMMC（eMMC、HS200/HS400、SDIO slave） | H417 | 2 | 単発 | eMMC の板 | PS:103, PM:246 |
| FSMC / FMC（SRAM、NOR、NAND、LCD、SDRAM） | V205、V30x、V4x7、H417 | 2 | 単発 | 外部メモリの板 | PS:102, PM:245 |
| PSRAM（チップ内 4〜8 MB） | V467 | 2 | 単発→自動 | V467 の石（自己） | PS:109, PM:254 |
| キーと値の保存（Preferences 相当）・EEPROM.h | 全系列 | 決定済み: 作らない（SYS-01、推奨は外部の SD・I2C の EEPROM） | — | 自己 | FC:29, TODO:228 |
| 内蔵 Flash 上のファイルシステム（littlefs）、FAT を NOR に、SPIFFS | 全系列（大きい系列） | 決定済み: 作らない（STO-04、2026-11-04） | — | 自己（電源断の試験は uhubctl） | FC:92, docs/filesystem.ja.md:255-258 |
| eMMC・USB ホストの MSC のブロックデバイス（FS の下） | H417、ホスト | 2 | 単発→自動 | eMMC の板、Pico の MSC | libraries/FS/README.ja.md:38, docs/filesystem.ja.md:286 |
| フラッシュの書き換え（UPD-01 自己更新、Update API） | 全系列 | 2 | 自動 | 自己 | FC:97 |
| ITCM / DTCM の配置の仕組み | H41x | 2 | 自動 | 自己 | TODO:172,176 |

## 特殊な周辺

| 機能 | 系列 | 優先 | 試験 | 要るもの | 根拠 |
|---|---|---|---|---|---|
| DMA の公開 API（メモリ間、ダブルバッファ、周辺へ） | 全系列 | 2 | 自動 | 自己 | PS:50, PM:201 |
| CRC の公開 API | V103、V20x、V205、V30x、V4x7、L103、X3x5、H41x | 1 | 自動 | 自己（既知のデータ） | PS:55, PM:210 |
| RNG の公開 API（`esp_random()`、`randomSeed()` の種） | V30x、V4x7、H41x | 1 | 自動 | 自己 | PS:115, PM:260 |
| ECDC（AES-128/192/256、SM4、ECB・CTR） | H417 | 2 | 自動 | 自己（既知のベクトル） | PS:56, PM:211 |
| PIOC（RISC8B の 8 bit コア） | X035、V205、H41x | 保留（ユーザー、2026-11-11: 今はやらない） | — | プローブ（1-Wire・IR・UART の波形） | PS:79, PM:215 |
| ARGB の `rgbLedWrite()`（ソフト、どの端子でも） | 全系列 | 1 | 自動 | プローブ（0.35 µs の High を測る） | FC:138, PLAN:44 |
| DVP（カメラ入力） | V30x（100 ピン）、V4x7、H417 | 2 | 単発→自動 | カメラ、またはプローブが PCLK・データを出す | PS:111, PM:256 |
| LTDC（RGB の LCD 出力） | V4x7、H417 | 2 | 単発→自動 | プローブ（取り込み）か LCD | PS:112, PM:257 |
| GPHA（2D DMA） | H417 | 2 | 自動 | 自己（メモリ間） | PS:113, PM:258 |
| SerDes（1.5 Gbps） | H417 | 2 | 自動 | 2 台目の H417 と差動の線 | PS:107, PM:252 |
| UHSIF の残り（スレーブのモード、出力、外部クロック、24 bit、制御線の出力） | H41x | 2 | 単発→自動 | 外のマスター（FPGA）か 2 台目、プローブ | PS:106, libraries/CH32RVUhsif/README.ja.md:19 |
| CH32RVLogicCapture の残り（深いリング、外部クロック、8/32 bit、出力） | 全系列 | 2 | 自動 | プローブ | TODO:147,424 |
| LVDS | V205 | 2 | 単発 | 未定（用途の確認から） | PS:117, PM:306 |
| ベクトル拡張（RVV 1.0、Zve64x） | V407/V467 | 2 | 自動 | 自己 | UNUSED:15,97 |
| 高耐圧の電源とゲートドライバ | M030（ハーフブリッジ 4 組）、M007（3 相） | 2 | 単発 | モーター基板（壊す恐れ。専用チップで、事前に聞く） | UNUSED:98 |
| モーター制御（FOC、SVPWM）・IQmath | L103、M030、V006、V20x | 3（範囲の判断） | 単発 | モーター | docs/wch-evt-binary-libraries.ja.md:35-36 |
| 小型 LCD の GUI（WUI）、音声認識、JPEG のデコード | L103、V205、V20x、V006、V307、V407、H417 | 3（範囲の判断） | 単発 | LCD、マイク | docs/wch-evt-binary-libraries.ja.md:37-38, EVT APPLICATION/JPEG_decode_LTDC |

## マルチコア

| 機能 | 系列 | 優先 | 試験 | 要るもの | 根拠 |
|---|---|---|---|---|---|
| HSEM・IPC の公開 API | H417/H416 | 1 | 自動 | 自己 | PS:66, PM:269-271 |
| V3F から使える周辺（Serial など） | H41x | 1 | 自動 | 自己 | libraries/CH32RV/src/CH32RVSystem.h:201-202 |
| V5F で休止・restart・WDT・フラッシュ、DFU の BOOT 域からの V5F | H41x | 2 | 自動 | 自己＋PC（DFU） | TODO:165-166 |
| H41x の休止で V5F・V3F を分けて止める | H41x | 2 | 単発 | 電流計 | libraries/CH32RV/README.ja.md:71 |

## ソフトウェアのライブラリ・範囲

| 機能 | 系列 | 優先 | 試験 | 要るもの | 根拠 |
|---|---|---|---|---|---|
| RTOS の port（FreeRTOS、RT-Thread、HarmonyOS、TencentOS） | 9 family（EVT のある系列） | 3（範囲の判断） | 自動 | 自己 | PS:132-135, PM:272-275 |
| WCH EVT / SPL 互換 API | 全系列 | 3（範囲の判断） | 自動 | 自己（EVT の例をビルド） | docs/project-scope.ja.md:28, FC:149 |
| ベンダ IDE とのバイナリ互換 | 全系列 | 3（範囲の判断） | 単発 | MounRiver のプロジェクト | docs/project-scope.ja.md:30, FC:151 |
| センサ・表示器・ファイルシステム等の汎用ドライバの同梱 | — | 3（範囲の判断） | 単発 | 部品ごと | docs/project-scope.ja.md:31, FC:148 |
| Arm 系の CH32F | CH32F | 3（範囲の判断） | 単発 | CH32F の石 | docs/project-scope.ja.md:26, FC:150 |
| 無線 SoC（CH32V208W の無線、CH58x、CH59x） | — | 3（範囲の判断） | 単発 | （除外の可能性） | docs/project-scope.ja.md:27, FC:146 |
| software 実装の方針（hardware を隠す・割込みを長く止めるものは入れない） | — | 3（範囲の判断） | — | — | docs/software-peripherals.ja.md:25-28 |

## 道具・書き込み・デバッグ

| 機能 | 系列 | 優先 | 試験 | 要るもの | 根拠 |
|---|---|---|---|---|---|
| 工場 UART ISP での書き込み | 全系列 | 2 | 単発 | USB-UART 変換、BOOT の端子（手作業） | docs/upload-routes.ja.md:19 |
| 独自 bootloader（UF2 / UART） | 全系列 | 2 | 自動 | PC | docs/upload-routes.ja.md:21 |
| user flash に置く自前の DFU bootloader（BOOT0 だけの系列向け） | V20x、V30x、V103、L103、V205、M030、V4x7 | 2 | 自動 | PC | docs/upload-routes.ja.md:174 |
| 書き込み経路の未確認（参考。実装はある） | V002/V004/V005/V007/M007/V208/V303/V305/V317/M103/X033/X305/V467/H415/H416 | 2 | 単発 | 各石 | docs/upload-routes.ja.md:58-76 |
| Windows の ch32rv-toolchain の大きさ | Windows | 2 | 単発 | Windows | TODO:211 |

計 184 行（BLE の除外を含む）。
