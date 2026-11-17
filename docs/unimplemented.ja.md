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
| HSE（外付け水晶）の残りの系列 | M030、X315（X305）、V407/V467 | 実装済み（2026-11-17、ビルドのみ。水晶は EVT の前提と回路図の Y1: M030 8 MHz、X3x5 20 MHz、V4x7 25 MHz。X3x5・V4x7 は EVT の HSE の手順。V4x7 の 20 MHz は HSI のまま。selfcheck::test_clock_source を clocksrc=hse で流す） | 単発→自動 | 水晶の載った板（EVT 板）。自己（selfcheck-clocks で RCC を読む＋時間の速さ） | PS:62, PM:294 |
| HSE の周波数の選択（EVT の前提以外の水晶） | HSE のある全系列 | 2 | 単発→自動 | 別の周波数の水晶の板。自己 | README.ja.md:49 |
| HSI の外での MCO（クロック出力） | 全 12 family | 2 | 自動 | プローブ 1 本（周波数を測る） | PS:61, PM:292 |
| 実行中のクロック変更 | 全系列 | 決定済み: 作らない（SYS-05） | — | 自己（変更前後で millis・Serial・PWM の selfcheck） | PS:40, FC:33 |
| H41x・X305 の CPU Clock メニュー | H415/H416/H417、X305 | 実装済み（2026-11-17、ビルドのみ。H41x は V5F / V3F の組で 400/100（既定）・200/50・25/25 MHz、F_CPU は CPU Core で決まる。X305 は X315 と同じ 240 / 20 MHz） | 自動 | 自己（selfcheck） | PS:40, TODO:504 |
| HSI / LSI の校正（トリム）API | V103、V20x、V205、V30x、V407、L103、M030、H417 | 2 | 自動 | 自己（HSE か RTC と比べる）、またはプローブで周波数 | EVT RCC/HSI_Calibration, RCC/LSI_Calibration |
| LSE（32 kHz 水晶） | RTC のある系列 | 2 | 単発→自動 | LSE の載った板。自己 | PS:53 |
| PVD（電圧低下の検出・割込み・起床） | V003〜H417 の 11 family | 実装済み（2026-11-17、ビルドのみ。`CH32RV.setLowVoltageDetect(mV, cb)` / `clearLowVoltageDetect()` / `isLowVoltage()` / `lowVoltageLevel()`、起床は `enableExtiLineWakeup(CH32RV_EXTI_PVD)`。H41x は PVD の割込み（EXTI の線）が無く、V004 / V007 / M007 は device-data に段が無いので未対応のまま） | 単発→自動 | 自己（selfexti::test_pvd: 閾値を VDD の上と下に。3.3 V の板で上にできるのは V003・V103・X035）。本当の低下は可変電源（VDD を下げる。単発） | PS:60, PM:290 |
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
| PFIC の割込み優先度の設定 | 全系列 | 実装済み（2026-11-17、ビルドのみ。`ch32rv_priority.h` の `ch32rv_irq_set_priority()`、コアの割り込みとの約束は CH32RV の README） | 自動 | 自己（タイマー 2 本で入れ子を確かめる）。selfapi::test_priorities | libraries/CH32RV/README.ja.md |
| VTF（高速割込み）、割込みの入れ子、ベクタを RAM へ | 全系列 | 2 | 自動 | 自己（遅延を SysTick で測る） | EVT INT/Interrupt_VTF, INT/VectorInRAM |
| オプションバイト（読み出し保護・書き込み保護・DATA0/1・START_MODE・STANDYRST/STOPRST・USARTDLEN 等・PORCTR・RST_PIN_SEL・CFGCANM） | 全 12 family | 2 | 自動 | 自己（読み戻し）。保護は解くとフラッシュが消えるので専用チップ | PS:69, PM:308 |
| PMP（メモリ保護） | V20x、V30x、V4x7、L103、X035、H41x | 2 | 自動 | 自己（禁止域に触れて例外） | PS:63, PM:296 |
| フラッシュの ECC の診断 | M030 | 2 | 自動 | 自己 | EVT CH32M030 FLASH/FLASH_ECC, TODO:318 |
| BootAsUser（BOOT 域をユーザーに） | V003、V006、X035 | 2 | 自動 | 自己 | EVT FLASH/BootAsUser |
| ADC・I2C のクロック範囲（バスを分周できない系列） | X315、H417、M030 | 2 | 自動 | プローブ（I2C の SCL） | TODO:190 |

## GPIO・割込み

| 機能 | 系列 | 優先 | 試験 | 要るもの | 根拠 |
|---|---|---|---|---|---|
| GPIO 以外の EXTI の線（PVD・USB 起床など。RTC のアラームは `CH32RV.setAlarm()` が使う） | 全系列 | 実装済み（2026-11-17、ビルドのみ。`CH32RV.attachExtiLine()` / `detachExtiLine()` / `triggerExtiLine()` / `enableExtiLineWakeup()`、線の名前は `CH32RV_EXTI_<NAME>`（internal/ch32rv_exti_line.h。device-data に EXTI の線の表が無いので RM と EVT から手で）。RTC アラームの線は setAlarm() と共有。PVD は `setLowVoltageDetect()` / `isLowVoltage()`、段は生成（operating_conditions の V_PVD）。H41x は PVD の割込みが無く、V004 / V007 / M007 は段のデータが無い。AWU の線は休止のもので出さない） | 単発→自動 | 自己（selfexti: 全線の SWIEVR、RTC アラームの線、その線での起床、PVD の上下）、PVD の本当の低下は可変電源（単発） | PS:37, FC:36 |
| EXTI のイベントモード（割込みでなく起床イベント） | 全系列 | 2 | 自動 | プローブ | PS:37 |
| GPIO の速さ・駆動力の設定 | 全系列 | 2 | 自動 | プローブ（立ち上がり時間。速い取り込み） | EVT GPIO/GPIO_Toggle |
| PWM の remap 先・CHxN の端子 | TIM のある全系列 | 実装済み（2026-11-17、`analogWrite()` が variant の `CH32RV_TIMER_PADS` のどの端子でも。ベンチ未確認） | 自動 | プローブ。signals::test_pwm_route、test_pwm_complementary | cores/arduino/wiring_pwm.c, PS:48, FC:70 |
| TIM8〜10 の PWM の実機 | V30x、V4x7、H41x | 2 | 自動 | プローブ | PS:48 |
| 既定以外の route の実機（X305/X315 の Wire など） | X305/X315 ほか | 2 | 自動 | プローブ＋プルアップ | TODO:498 |
| SD_MMC の V4x7 の remap、H41x の AF の端子 | V4x7、H41x | 実装済み（2026-11-17、ビルドのみ。variant の `CH32RV_SDIO_PADS` から `setPins()` が選ぶ。H41x は端子ごとの AF、V4x7 は PCFR2 の SDIO_RM） | 単発 | SD カードの口のある板 | libraries/SD_MMC/README.ja.md:52 |

## タイマー・PWM

| 機能 | 系列 | 優先 | 試験 | 要るもの | 根拠 |
|---|---|---|---|---|---|
| 相補PWM（CHxN）・デッドタイム・ブレーキ | 全 12 family | 実装済み（2026-11-17、CH32RVTimerOutput の `CH32RVComplementaryPwm`。ベンチ未確認） | 自動 | プローブ 2〜3 本（CHx と CHxN の重なり）。ブレーキはプローブが BKIN を動かす。signals::test_complementary | libraries/CH32RVTimerOutput, PS:59, PM:280 |
| 非対称デッドタイム | M030、H417 | 2 | 自動 | プローブ | UNUSED:85, EVT TIM/Dead_Asymmetry |
| ワンパルス | 全系列 | 実装済み（2026-11-17、`CH32RVOnePulse`。`fire()` と CH1/CH2 のエッジでの開始。ベンチ未確認、試験は `fire()` だけ） | 自動 | プローブ（幅を測る）。signals::test_one_pulse | libraries/CH32RVTimerOutput, PS:59, EVT TIM/One_Pulse |
| 出力比較（Output Compare） | 全系列 | 実装済み（2026-11-17、`CH32RVOutputCompare`。ベンチ未確認） | 自動 | プローブ。signals::test_output_compare | libraries/CH32RVTimerOutput, PS:59, EVT TIM/Output_Compare_Mode |
| 6 ステップ PWM、位相シフト PWM、中央揃え、独立出力 | V003、V103、V20x、V205、V30x、V407、M030 | 2 | 自動 | プローブ | EVT TIM/PWM_6_Step, PWM_Phase-shifted |
| タイマーの同期・連結（マスター／スレーブ、外部トリガで開始） | 全系列 | 2 | 自動 | プローブ | UNUSED:84, TODO:301 |
| ETR（外部クロック）・外部パルスの計数 | 全系列 | 2 | 自動 | プローブ（クロックを入れる） | PS:48, FC:69 |
| タイマーの DMA バースト | 全系列 | 2 | 自動 | プローブ | PS:48, EVT TIM/TIM_DMA |
| 入力捕捉: エッジごとの時刻・通知 | 全系列 | 実装済み（2026-11-17、CH32RVTimerInput の `CH32RVInputCapture`、キューか callback。ベンチ未確認） | 自動 | プローブ。signals::test_edge_capture | libraries/CH32RVTimerInput, PS:57, FC:68 |
| 両エッジの捕捉・CAPLVL（捕捉時のレベル） | M030、H417、V003、V00x、X035、V205、L103、X315 | 2 | 自動 | プローブ | UNUSED:42,85, EVT TIM/Bothedge_Capture |
| 入力捕捉・エンコーダの CH1/CH2 以外の端子 | 全系列 | 実装済み（2026-11-17、`CH32RVInputCapture` はどのチャネル・経路でも、PulseCapture とエンコーダは remap 先も。PWM 入力モードとエンコーダモードはハードが CH1/CH2 だけ。ベンチ未確認） | 自動 | プローブ。signals::test_edge_capture | libraries/CH32RVTimerInput, PS:57-58, FC:69 |
| `pulseIn()` のハード化 | 全系列 | 実装済み（2026-11-17、選んで使う `timerPulseIn()`（CH32RVTimerInput）。`pulseIn()` は全端子で software のまま（ユーザー 2026-11-17: 大きさを増やさない）。ベンチ未確認） | 自動 | プローブ。signals::test_timer_pulse_in | libraries/CH32RVTimerInput, PS:57 |
| 利用者向けタイマー API（周期・単発・時間単位） | 全系列 | 実装済み（2026-11-17、arduino-esp32 3.x の `timerBegin()`・`timerAlarm()` など、`CH32RVHwTimer.h`。ベンチ未確認） | 自動 | プローブ（線を取り込む）。signals::test_hw_timer | cores/arduino/ch32rv_hw_timer.c, FC:66 |
| Ticker 相当 | 全系列 | 実装済み（2026-11-17、Ticker ライブラリ、arduino-esp32 の API。ベンチ未確認） | 自動 | プローブ（線を取り込む）。signals::test_ticker | libraries/Ticker, FC:67 |
| ADC をタイマーで捕捉（TIM/ADC_Capture） | M030 | 2 | 自動 | プローブ | EVT CH32M030 TIM/ADC_Capture |
| 32 bit の TIM4 | V205、X315、L103 | 2 | 自動 | 自己 | UNUSED:85 |
| LPTIM（休止中の計時・起床、PWM・ワンパルス） | L103、H417 | 2 | 自動 | プローブ（起床・波形） | PS:49, PM:200 |
| SLTIM（V006 の小さいタイマー）と ADC トリガ | V006（V00x） | 2 | 自動 | プローブ | EVT CH32V006 TIM/SLTIM_DMA, ADC/SLTIM_Trigger |
| SoftPWM | 全系列 | 2 | 自動 | プローブ | docs/software-peripherals.ja.md:20, FC:73 |

## アナログ

| 機能 | 系列 | 優先 | 試験 | 要るもの | 根拠 |
|---|---|---|---|---|---|
| ADC のタイマー・外部トリガ | 全系列 | 実装済み（2026-11-17、ビルドのみ。CH32RVAdcStream の `beginTimer()`・`beginExternal()`・`conversions()`、arduino-esp32 の `analogContinuous()`。トリガ源の表は EVT から core に（internal/ch32rv_adc_trigger.h）、device-data に EXTSEL の意味の表が無い） | 自動 | プローブ（方形波を入れて間隔を見る）。periph::test_adc_timer_trigger・test_adc_external_trigger（トリガの端子のラベルが要る）・test_analog_continuous | libraries/CH32RVAdcStream, PS:80, PM:282 |
| ADC の注入変換・不連続モード | 全系列 | 2 | 自動 | プローブ | PS:80, EVT ADC/Auto_Injection・Discontinuous_mode |
| ADC アナログウォッチドッグ | 全 12 family | 2 | 自動 | プローブ（電圧を動かす）。自己なら DAC の系列 | PS:81, PM:284 |
| ADC デュアル（同時・交互） | V20x、V30x、V4x7、X3x5、H41x | 2 | 自動 | プローブ 2 本 | PS:82, PM:286 |
| ADC の PGA（入力増幅） | L103、V205、H417 | 2 | 単発 | 外部の電圧源（精度はマニュアル試験） | UNUSED:74, EVT ADC/ADC_PGA |
| ADC の平均（ADC_AVG）・高速変換 | M030、L103 | 2 | 自動 | プローブ | EVT CH32M030 ADC/ADC_AVG, CH32L103 ADC/ADC_FastConvent |
| X315 の ADC の縦続・走査出力・注入の交互 | X305/X315 | 2 | 自動 | プローブ | EVT CH32X315 ADC/ADC_Cascade・ADC_ScanOut・ADC_InjeAlter |
| 外付け CH448 で 384 チャネル | X305/X315 | 2 | 単発 | CH448 の板 | UNUSED:75 |
| mV を返す `analogReadMilliVolts()` | 全系列 | 実装済み（2026-11-17、ビルドのみ。直前に Vrefint で測った電源電圧で換算。精度は未測定） | 単発（精度）。自動は広い幅だけ（selfapi::test_millivolts） | 外部の電圧源（マニュアル） | libraries/CH32RV/README.ja.md |
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
| UART の DMA 受信・送信 | 全系列 | 実装済み（2026-11-17、ビルドのみ。`setRxBufferSize()`・`setTxBufferSize()`（ESP32 の名前、begin() の前）のバッファを DMA が運ぶ。受信は循環の DMA、送信は TC の割り込みで次の転送。チャネルは device-data の dma.csv、H41x は生成器が選ぶ。`ch32rv_dma_claim()` で CH32RVAdcStream・CH32RVLogicCapture と取り合う） | 自動 | プローブ。uart-dma::test_uart_dma_receive・test_uart_dma_interrupts_off・test_uart_dma_transmit・test_uart_dma_receive_event | cores/arduino/HardwareSerialDmaRx.cpp, HardwareSerialDmaTx.cpp, EVT USART/USART_DMA |
| UART の IrDA（`setMode(UART_MODE_IRDA)`、今は false） | 全系列 | 2 | 自動 | IrDA のパルスを読める相手（プローブで取り込み） | EVT の USART に例なし、RM の IREN |
| UART 同期モード（クロック出力）・スマートカード・マルチプロセッサ（アドレス起床） | 全系列（スマートカードは V20x、V205、L103、X035） | 2 | 自動 | プローブ（同期は SPI として取り込む） | EVT USART/USART_SynchronousMode・USART_SmartCard・USART_MultiProcessorCommunication |
| SoftwareSerial | 全系列 | 実装済み（2026-11-17、ライブラリ。ベンチ未確認） | 自動 | プローブ。uart::test_uart_software_serial | libraries/SoftwareSerial, FC:54 |
| I2C マルチマスター | 全系列 | 実装済み（2026-11-17、Wire。負けたらバスが空くのを待ってやり直し、3 回まで、毎回負ければ 4。ベンチ未確認。わざと負ける試験はプローブで作れずスキップ） | 自動 | プローブ（i2c-controller をもう 1 つのマスターに）。wire::test_wire_controller_and_target（起きた衝突を数える）、wire::test_wire_arbitration_lost（スキップ: START を 1 ビット以内にそろえる手段が要る、[oep] への依頼はまだ） | FC:47, libraries/Wire/src/Wire.h |
| I2C のマスターとスレーブの兼用 | 全系列 | 実装済み（2026-11-17、Wire の 1 インスタンス。SoftWire は 1 つずつ。ベンチ未確認） | 自動 | プローブ（i2c-controller と i2c-target を入れ替え）。wire::test_wire_controller_and_target | FC:48, libraries/Wire/src/Wire.h |
| I2C の 10 bit アドレス・General Call | 全系列 | 実装済み（2026-11-17、`WIRE_10BIT`・`setGeneralCall()`、Wire と SoftWire の controller・target。ベンチ未確認） | 自動 | プローブ（i2c-controller は 7 bit の欄で 0x78〜0x7B と 0 を送れる。i2c-target は予約アドレスを断るので、controller の 10 bit はヘッダーの NACK まで）。wire::test_wire_10bit・test_wire_general_call | FC:49, libraries/Wire/src/Wire10Bit.cpp |
| I2C の PEC（SMBus）・DMA | 全系列 | PEC は実装済み（2026-11-17、`setPEC()`、ソフトウェアの CRC-8 で Wire・SoftWire 共通。ベンチ未確認）。DMA は 2（未実装。ユーザー: 任意、2026-11-17。転送はバッファの 32 バイトまでなのでポーリングと割り込みで足りている） | 自動 | プローブ（i2c-target）。wire::test_wire_pec | FC:58, libraries/Wire/src/TwoWirePec.cpp |
| Wire の NACK・timeout・回復・clock stretch の試験 | 全系列 | 試験を追加（2026-11-17、ベンチ未確認）。データの NACK だけはプローブで作れずスキップ | 自動 | プローブ。wire::test_wire_timeout（gpio で SCL を握る、i2c-target の stretch でタイムアウト超え）・test_wire_clock_stretch・test_wire_bus_recovery（DUT が読みの途中で止めて相手に SDA を握らせる）・test_wire_data_nack（スキップ: i2c-target に指定のバイトから NACK する設定が要る、[oep] への依頼はまだ） | TODO:27, TC:92 |
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
| ARGB（WS2812 のハードウェア） | V4x7、X3x5 | 実装済み（2026-11-17、ビルドのみ。同じ `rgbLedWrite()` / `ch32rv_rgb_led_write()` が ARGB の端子（V4x7 は PA15、X3x5 は PA15・PD3・PD4。variant の `CH32RV_ARGB_PIN_TO_AF`）ではブロックと DMA で送り、割り込みを止めない。ほかの端子はソフトのまま。終わりまで待つ（同期）。`ch32rv_rgb_led_is_hardware()` で分かる） | 自動 | プローブ（60 MS/s、WireSkein の rgb_led）。signals::test_rgb_led_hardware（ARGB の端子に `<口>.argb@<端子>` か `<口>@<端子>` の線）。割り込みが止まらないことは selfsys::test_rgb_led_irq（配線なし） | PS:114, PM:259 |

## USB デバイス

| 機能 | 系列 | 優先 | 試験 | 要るもの | 根拠 |
|---|---|---|---|---|---|
| ファームウェア更新のクラス（DFU の download、MSC の UF2 = MscFirmwareDisk） | 全 USB 系列 | 実装済み（2026-11-17、ビルドのみ。`CH32RVUsbDeviceDfu`、`CH32RVUsbDeviceMscFirmwareDisk`、Update ライブラリ。スケッチの後ろの flash に置き、確かめて再起動でコピー） | 自動 | PC（usb-firmware の case。CH32RV_BOOT_HOLD_MS 付き） | docs/usb.ja.md「ファームウェア更新」, cores/arduino/ch32rv_update.h |
| ソフトウェア USB デバイス（rv003usb） | V003（V00x） | 2 | 自動 | USB 端子の配線（D+/D- を GPIO に）＋PC | FC:116, TODO:479 |
| 工場 ISP への入口（enterBootloader） | H41x | 2 | 自動 | PC（ch32rv isp） | docs/usb.ja.md:371, FC:115 |
| 工場 ISP への入口（BOOT_MODE の無い系列） | V20x、V30x、V103、L103、V205、M030、V4x7 | 2 | 自動 | PC | docs/usb.ja.md:372-373, docs/upload-routes.ja.md:174 |
| USBSS のバルクストリーム（UAS。デバイスもホストも） | H417/H416、X315 | ハードの限界（2026-11-14 の実験） | — | デバイスが MaxStreams 2 を名乗ると PC の xHCI はストリームを割り当てるが、ストリーム 1 の転送は OUT・IN とも時間切れで、カーネルは「Transfer event for unknown stream ring」。RM 27 章と EVT のヘッダーに Stream ID の欄が無い。ホストの側にも欄が無い（HCD の下書きで確認） | TODO:136-137 |
| USBSS デバイスの等時の Mult>0（1 間隔に複数 burst） | H417/H416、X315 | 実装済み（2026-11-17、ビルドのみ）。ハードに Mult の欄は無いが要らない: ホストが burst ごとに ACK TP で求め、DCD は 1 間隔を 1 つの chain（EXP_NUMP / MAX_NUMP は 8 bit、最大 48 パケット）で出し入れし、最後のパケットに LPF（TX CHAIN_ST.EOB_LPF）。EVT の SS カメラ（UVC-DVP）も bMaxBurst 15・Mult 2 で 1 間隔 32 パケットを 1 chain で送る。記述子は `CH32RVUsbDescriptorWriter::superSpeedBurst(maxBurst, mult, bytesPerInterval)` | 自動 | PC（libusb の等時）: usb-ss-iso（Mult 0/1/2、IN・OUT、1 間隔のバイト数と抜け）。6-2 で未実行 | libraries/CH32RVUsbDevice/src/CH32RVUsbDcdUsbss.cpp, tests/instrumented/usb/test_usb_ss_iso.py |
| USBSS の U1/U2/U3 | H417/H416、X315 | 2 | 自動 | PC（Linux の LPM） | libraries/CH32RVUsbDevice/src/CH32RVUsbDcdUsbss.cpp:26,207,263 |
| V20x の USBD と USBFS を同時に使う（2 つのデバイス） | V203/V208 | 2 | 自動 | PC の口 2 つ | libraries/CH32RVUsbDevice/src/CH32RVUsbDevice.h:44-45 |
| V407 の 2 つのデバイス・デバイスとホストの同時 | V407/V467 | 2 | 自動 | PC＋周辺機器 | docs/usb.ja.md:347, EVT CH32V407 USBHS/Dual_device・Device_Host |
| USBSS の口の USB 2.0 側（同じコネクタの HS への切り替え） | H417/H416 | 2 | 自動 | PC、SWD を外す部品 | libraries/CH32RVUsbDevice/src/CH32RVUsbDcdUsbss.cpp:29-33, TODO:135-136 |
| EspUsbDevice との差（複数リスナー、キュー、64 bit LBA、CCID の追加 API、Info の例、例 30 本ほど） | 全 USB 系列 | 2 | 自動 | PC、ESP32-S3 | TODO:468 |
| USB デバイスのコールバックが関数ポインタ 1 枠（`std::function`・複数登録なし） | 全 | 2 | 自動 | 自己 | libraries/CH32RVUsbHost/README.ja.md:94 |
| suspend / resume・リモートウェイクアップの試験 | 全 USB 系列 | 実装済み（2026-11-17、ビルドのみ。usb-power の case。USBFS の OTG の配置と V103 のリモートウェイクアップを足した） | 自動 | PC（Linux のランタイム PM。power/control を書ける udev のルールが要る: docs/usb.ja.md「サスペンド・レジューム・リモートウェイクアップ」） | libraries/CH32RVUsbDevice/src/CH32RVUsbDevice.h:123-125, TC:139 |

## USB ホスト

| 機能 | 系列 | 優先 | 試験 | 要るもの | 根拠 |
|---|---|---|---|---|---|
| V103 の USB（旧 IP）のホスト | V103 | 実装済み（2026-11-17、ビルドのみ。USBFS の HCD の 3 つ目の配置） | 自動 | Pico か ESP32-S3（usb-peer-host）。V103 の台の USB の口（PA11 / PA12）から Pico への配線待ち（TEST_PLAN） | COV:179-188,196,204,212, TODO:473 |
| 第 1 世代 USBHS のホスト | V305/V307/V317 | 実装済み（2026-11-17、ビルドのみ。CH32RVUsbHcdUsbhs1.cpp、`HIGH_SPEED`。この IP の split は SPLIT の欄の説明が無く未対応） | 単発→自動 | ESP32-P4（HS のデバイス）、HSE の板。V307 の USBHS（PB6 / PB7）から P4 への配線待ち | libraries/CH32RVUsbHost/src/CH32RVUsbHostPort.h:17, libraries/CH32RVUsbHost/README.ja.md:47-48 |
| USBSS のホスト（リンク・制御・バルク・ERDY・burst・割込み・等時の Mult・SS ハブ・warm reset・U1〜U3 の許可） | H417/H416（X315 はシリコンがデバイスのみ） | 実装済み（2026-11-14、ビルドのみ。ストリームはハードの限界: 上の USBSS のバルクストリームの行） | 単発→自動 | H417 どうしの直結（片方 DCD）、USB 3.0 メモリ（手作業）。TEST_PLAN の「USB の網羅の軸」の 3 台の H417 の配線待ち | libraries/CH32RVUsbHost/src/CH32RVUsbHcdUsbss.cpp |
| HS ハブの先の FS・LS（split トランザクション） | 第 2 世代 USBHS のホスト（V205、V4x7、X3x5、H41x） | 実装済み（2026-11-17、ビルドのみ。SPLIT のトークンはハード、開始と完了の split はドライバ。CLEAR_TT_BUFFER と 188 バイトを超える等時の OUT は無し） | 自動 | HS ハブ＋FS の Pico。V407 の USBHS1 → USB2 の PPPS ハブ（マルチ TT）→ Pico の配線待ち（記録の `hub: hs-mtt`） | libraries/CH32RVUsbHost/README.ja.md:66, docs/usb.ja.md:344 |
| ハブ越しの LS 機器（PRE PID）の確認 | USBFS のホスト | 2 | 単発 | FS ハブ＋LS の機器 | TODO:472-473 |
| ホストのアイソクロナス（オーディオ・ビデオ） | 全ホスト | 1 | 自動 | ESP32-S3 / P4 の UAC・UVC デバイス | libraries/CH32RVUsbHost/README.ja.md:99, libraries/CH32RVUsbHost/src/CH32RVUsbHost.h:31 |
| ホストの USB ネットワーク（NCM / ECM） | 全ホスト | 1 | 自動 | ESP32-S3 の NCM か USB の LAN アダプタ | FC:113, TODO:472 |
| ホストの MSC のファイルシステム（マウント） | 全ホスト | 1 | 自動 | USB メモリか、MSC デバイスの対向（ESP32-S3・Pico・CH32RV） | libraries/CH32RVUsbHost/README.ja.md:100、libraries/FS/README.ja.md:38、docs/filesystem.ja.md:286、FC:93 |
| ホストの USB シリアルの `setRxBufferSize()` | 全ホスト | 2 | 自動 | Pico | libraries/CH32RVUsbHost/README.ja.md:102 |
| H41x の USBHS ホスト（SWD と兼用、口の 2.0 側） | H41x | 実装済み（2026-11-17、ビルドのみ。DMA の届く範囲の確かめが 17 ビットのままで H41x では `begin()` が必ず Busy だったのを直した: H41x・X3x5 は 24 ビット） | 単発 | ESP32-P4、SWD を外す部品。`begin()` の後はデバッガがつながらない（起動待ちのイメージで試す） | docs/usb.ja.md:341, TODO:457 |
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
| SDIO（SD_MMC）の残り: DMA、high speed、ハードのフロー制御、H41x の AF・V4x7 の remap、MMC 4/8 bit | V30x、V4x7、H41x | 実装済み（2026-11-17、ビルドのみ。ブロックは DMA2 の 4 番（H41x は DMAMUX で同じ channel）を `ch32rv_dma_claim()` で取って、取れないときと 4 バイト境界でない buffer は CPU。SD の high speed は CMD6、MMC は CMD1・EXT_CSD・BUS_WIDTH 4/8・HS_TIMING。端子は variant の `CH32RV_SDIO_PADS`（生成、internal/ch32rv_sdio.h）。フロー制御は既定で on、`card().setFlowControl(false)` で比べる。V30x の ETHMACEN で D0/D1 が PB14/PB15 に移る道は使わない） | 自動 | SD カードの口の板（V307 評価板） | libraries/SD_MMC/README.ja.md:40,50-53, PS:101 |
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
| CRC の公開 API | V103、V20x、V205、V30x、V4x7、L103、X3x5、H41x | 実装済み（2026-11-17、ビルドのみ。`CH32RV.crc32()`、ほかの系列はソフトで同じ値） | 自動 | 自己（既知のデータ）。selfapi::test_crc | libraries/CH32RV/README.ja.md |
| RNG の公開 API（`esp_random()`、`randomSeed()` の種） | V30x、V4x7、H41x | 実装済み（2026-11-17、ビルドのみ。`ch32rv_random()` / `ch32rv_fill_random()`、ほかの系列は暗号に使えないソフトの値。`random()` は Arduino のとおり） | 自動 | 自己。selfapi::test_rng | libraries/CH32RV/README.ja.md |
| ECDC（AES-128/192/256、SM4、ECB・CTR） | H417 | 2 | 自動 | 自己（既知のベクトル） | PS:56, PM:211 |
| PIOC（RISC8B の 8 bit コア） | X035、V205、H41x | 保留（ユーザー、2026-11-11: 今はやらない） | — | プローブ（1-Wire・IR・UART の波形） | PS:79, PM:215 |
| ARGB の `rgbLedWrite()`（ソフト、どの端子でも） | 全系列 | 実装済み（2026-11-17。LED 1 個は 7 系列でベンチ確認。チェーンは `ch32rv_rgb_led_write()` で 1 つのフレーム。72 MHz 以上は SysTick、それ未満は RAM の命令の数で時間を作る） | 自動 | プローブ（60 MS/s、WireSkein の rgb_led）。signals::test_rgb_led | libraries/CH32RV/README.ja.md |
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
| HSEM・IPC の公開 API | H417/H416 | 実装済み（2026-11-17、ビルドのみ。`CH32RV.hsemTake/TryTake/Release()`・`ipcSend/Receive/OnReceive()`、C は ch32rv_multicore.h。CPU Core = V3F では IPC は false） | 自動 | 自己（selfapi::test_cores_hsem・test_cores_ipc） | PS:66, PM:269-271 |
| V3F から使える周辺（Serial など） | H41x | 実装済み（2026-11-17、ビルドのみ。V3F の関数は割り込み有効で動き、V3F で始めた周辺の割り込みは V3F へ。共有のレジスタ（RCC・GPIO の設定・AFIO・EXTI）はコアのロック。約束は internal/ch32rv_v3f.h と CH32RV の README。V3F のヒープは未対応） | 自動 | 自己（selfapi::test_cores_v3f_serial） | libraries/CH32RV/src/CH32RVSystem.h:201-202 |
| CPU Core = V3F のときに V5F を 2 つ目のコアにする（V3F が主の形の `setup1()` / `loop1()`・`startV5F()`。今は V5F が眠ったままで IPC は false） | H41x | 2（案） | 自動 | 自己（selfapi の cores を CPU Core = V3F でも 2 コアで） | docs/h417-cores-design.ja.md（C7）、ch32rv_multicore.h |
| V3F のヒープ（malloc・new・String を 2 つのコアから。picolibc の malloc にロックが無い） | H41x | 2（案） | 自動 | 自己（2 つのコアで割り当てと解放を繰り返し、壊れないこと） | internal/ch32rv_v3f.h、libraries/CH32RV/README.ja.md |
| V3F を外から止める・起動し直す（arduino-pico の `restartCore1()` / `idleOtherCore()` に当たるもの） | H41x | 2（案） | 自動 | 自己（止めて、もう一度 `startV3F()` が通ること） | docs/h417-cores-design.ja.md:153 |
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
