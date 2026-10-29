# 追加機能の採否検討

作成日: 2026-10-17

ArduinoCore-CH32RV の機能範囲を、クラス単位と主要な動作モード単位で検討するための一覧。
ESP32 の Arduino Core のように、利用者が公開 API から幅広い用途を扱える構成を検討する。
ESP32 と同じハードウェア・クラス名・全 API の再現を前提にはしない。

**作成時（2026-10-17）は採否がすべて未決定だった。その後に入ったものは、2026-10-29 に現状と採否の欄を直した。**
一覧への掲載は実装の決定ではなく、既存の仕様・ADR・対応表を変更するものでもない。
現状欄の「対象外」は既存方針を表し、採否欄とは区別する。
既存 TODO に実装方針がある項目はその旨を記載し、今回の整理で既存方針を取り消したとは扱わない。

## 個別に決めること

- 採否: 実装する／対象外にする／保留。
- 実装する場合の提供場所: コア／同梱ライブラリ／独立ライブラリ／外部ライブラリとの接続。
- 対象の系列・型番と動作モード。表の同じ行でも、モードごとに分割して判断できる。
- 採否の理由と、他項目への依存。ID は判断の参照に使う。

全系列で同じ機能を提供することは前提にしない。搭載の有無、端子、メモリ、帯域は系列・型番ごとに確認する。
「API がある」と「全系列で実機確認済み」は区別する。
実装の内部品質、個別の不具合、ビルド・実機試験だけが残る項目は、この一覧の主対象にはしない。

## システムと基本機能

| ID | 検討する機能 | 現状 | 追加範囲と判断点 | 採否 |
|---|---|---|---|---|
| SYS-01 | 永続設定保存 | 内蔵フラッシュは `ch32rv_flash.h`（2026-10-28、[flash-store](docs/flash-store.ja.md)）。`EEPROM.h` と Preferences 相当は出さないと決めた（推奨は外部の SD・I2C の EEPROM） | キーと値の保存は、必要になったら | 一部採用 |
| SYS-02 | Flash 自己書き換え | `ch32rv_flash.h` を全系列で公開（範囲・1 単位・消して書く。2026-10-28） | 更新（UPD-01）での使い方 | 採用 |
| SYS-03 | 低消費電力 | `CH32RV.lightSleep` / `deepSleep`、タイマーと端子での起床、起床の理由（2026-10-27。AWU の系列と RTC の系列。V103 は外した） | ほかの系列、周辺を動かしたままの休止 | 一部採用 |
| SYS-04 | RTC | RTC の系列で `time()` / `settimeofday()` が RTC を読み書きし、リセットと standby をまたぐ。休止のアラームにも使う（2026-10-27） | カレンダー・アラームの公開 API、BKP の公開 API | 一部採用 |
| SYS-05 | クロック設定 | CPU Clock のメニュー（2026-10-22）と Clock Source（HSI / HSE、2026-10-27）。実行中の変更はしないと決めた | H41x・X305 のメニュー、各項目の実機確認 | 一部採用 |
| SYS-06 | ウィンドウウォッチドッグ | IWDG はあり、WWDG は未実装 | WWDG の開始・更新・状態通知。IWDG と別機能として提供するか | 未決定 |
| SYS-07 | バックアップレジスタ | 現行対応表では対象外 | 少量の状態保存、保持領域へのアクセス。RTC とまとめるか | 未決定 |
| SYS-08 | GPIO 以外の EXTI | GPIO 割り込みはあり、PVD・RTC・USB 起床等は未対応 | 非 GPIO イベント、復帰要因の登録・通知。SYS-03／04 と連携 | 未決定 |
| SYS-09 | 割り込み優先度の設定 | PFIC は使用するが優先度は reset 既定 | 利用者・ライブラリからの優先度設定、コアが使う割り込みとの契約 | 未決定 |

## 通信クラスの動作モード

| ID | 検討する機能 | 現状 | 追加範囲と判断点 | 採否 |
|---|---|---|---|---|
| BUS-01 | SPI スレーブ | 別ライブラリ CH32RVSPISlave（CS ごとに 1 回の転送をキューで。2026-10-27） | ハードウェアのマスターでの上限、V407・H417 の実機 | 採用 |
| BUS-02 | SPI 非同期転送 | 同期・ポーリング転送 | 転送開始、完了確認・通知、中止。DMA 利用と公開 API の形は分けて判断する | 未決定 |
| BUS-03 | SPI ハードウェア CS | CS はスケッチが GPIO として操作 | ハードウェアによる CS 制御。手動 CS と選択できる形を検討 | 未決定 |
| BUS-04 | SPI 半二重・片方向モード | 通常の全二重転送が中心 | 3線式双方向、送信専用、受信専用。バッファを捨てる処理と物理的な通信モードを区別する | 未決定 |
| BUS-05 | I2C マルチマスター | `Wire` はマスター／スレーブ対応、マルチマスター非対応 | バス調停、競合時の通知・復旧 | 未決定 |
| BUS-06 | I2C マスターとスレーブの兼用 | 1インスタンスはどちらか一方 | スレーブ待受とマスター送信の併用。BUS-05 との関係を確認 | 未決定 |
| BUS-07 | I2C 拡張アドレス・呼び出し | 通常の7bitアドレスを使用。General Call は受けない | 10bitアドレス、General Call を個別に判断 | 未決定 |
| BUS-08 | UART RS485 | 通常 UART はあるが専用の方向制御 API なし | 外付けトランシーバの DE／RE 制御、送信完了に合わせた方向切り替え | 未決定 |
| BUS-09 | UART 1線式半二重 | 公開モードなし | 同一信号線での送受信切り替え。RS485 とは別に判断 | 未決定 |
| BUS-10 | UART ハードウェアフロー制御 | `setPins()` の CTS／RTS は指定不可 | RTS／CTS の設定とフロー制御 | 未決定 |
| BUS-11 | UART 受信イベント・エラー通知 | バッファと `serialEvent` はある | 通信エラー、オーバーフロー、IDLE 等の受信区切り通知。既存 `serialEvent` との差を明確にする | 未決定 |
| BUS-12 | SoftwareSerial | ソフトウェア UART は未実装 | GPIO による送受信、対応速度・同時動作条件。SoftSPI／SoftWire は既存 | 未決定 |
| BUS-13 | CAN | 現行対応表では対象外。`HardwareCAN` は抽象クラスのみ | CH32 用ドライバ、標準／拡張 ID、フィルタ、Listen-only、Loopback、エラー状態。CAN FD はハードウェア対応確認後に別判断 | 未決定 |
| BUS-14 | I2S | 現行対応表では対象外 | 送信／受信、Master／Slave、対応データ形式・サンプルレート、連続ストリーミング | 未決定 |

SPI スレーブは CH32RV として検討する機能。比較した ESP32 標準 `SPIClass` にもスレーブ用公開 API はなく、
ESP32 標準との互換性不足とは区別する。

## タイマーとアナログ

| ID | 検討する機能 | 現状 | 追加範囲と判断点 | 採否 |
|---|---|---|---|---|
| TIM-01 | 利用者向けタイマー API | `CH32RVTimer.h` に資源管理、開始・停止、更新割り込みあり | 周期／単発、時間単位での設定、カウンタ取得。既存資源管理の上に使いやすい API を置く | 未決定 |
| TIM-02 | Ticker 相当 | 専用 API なし | 一定時間後・周期的なコールバック。ハードウェアタイマーとソフトウェアスケジュールのどちらを使うか、実行文脈を決める | 未決定 |
| TIM-03 | 入力キャプチャ | `CH32RVTimerInput` の `CH32RVPulseCapture`（周期・High の時間を `read()` で読む。割り込みなし。2026-10-28） | エッジごとの時刻とキャプチャ通知は、通知の配送の規則に合わせて後で（docs/peripheral-a-design.ja.md の 3） | 一部採用 |
| TIM-04 | エンコーダ・外部パルス計数 | `CH32RVTimerInput` の `CH32RVEncoder`（x4 / x2 / x1、64 ビット、ESP32Encoder の名前。2026-10-28） | 外部クロック（ETR）による計数、タイマーのチャネルの無い端子でのエンコーダ（EXTI で数える方式） | 一部採用 |
| TIM-05 | PWM の端子・タイマー範囲拡張 | TIM4 / TIM5 の既定ピン、TIM8〜10 の PWM（2026-10-22、ビルドのみ）。remap 先、CHxN は未対応 | remap、CHxN | 一部採用 |
| TIM-06 | 高度な PWM 制御 | 通常 PWM、tone、Servo あり | 相補出力、デッドタイム、Break、同期・位相制御。通常 PWM と用途を分ける | 未決定 |
| TIM-07 | 低消費電力タイマー | LPTIM は現行対応表では対象外 | Sleep 中の計時・起床、SYS-03／04 との役割分担 | 未決定 |
| TIM-08 | SoftPWM | 未実装。現行方針では独立 API にしない | ハードウェア PWM のない端子での出力。timer 資源・割り込み負荷の制約 | 未決定 |
| ANA-01 | ADC 連続取得 | CH32RVAdcStream（自走、1〜6 本、DMA の 2 面バッファ。2026-10-27） | タイマーのトリガでの遅い一定のレート、しきい値の通知 | 一部採用 |
| ANA-02 | ADC トリガ・監視 | 専用の利用者向け API なし | タイマートリガ、外部トリガ、しきい値監視。対応するハードウェア機能ごとに判断 | 未決定 |
| ANA-03 | ADC 電圧換算 | Vref・VDD・温度の補助 API はあるが、外部入力の mV 取得 API なし | `analogReadMilliVolts()` 相当。推定値と校正値の扱いを決める | 未決定 |
| ANA-04 | DAC 連続出力 | 対応系列で `analogWrite()` の値出力あり | バッファからの波形出力、タイマートリガ、繰り返し出力 | 未決定 |
| ANA-05 | OPA・コンパレータ | 現行対応表では対象外 | 内蔵アンプの経路・ゲイン、比較器の入力・出力・イベント。両者を個別に判断 | 未決定 |
| ANA-06 | TouchKey | `touchRead()` / `touchSetCycles()`（arduino-esp32 の名前、2026-10-27） | しきい値・イベント | 一部採用 |

対応表の「TIM（PWM/tone/入力捕捉）」は一括して実装済みになっているが、
入力キャプチャを利用者がレジスタ操作なしで使う API まで提供しているわけではない。
TIM-03／04 は資源管理 API が存在することと分けて判断する。

## ストレージとネットワークと更新

| ID | 検討する機能 | 現状 | 追加範囲と判断点 | 採否 |
|---|---|---|---|---|
| STO-01 | FS／File 共通 API | 汎用ファイルシステムは現行方針で同梱対象外 | ファイル・ディレクトリ操作の共通入口。独自実装、同梱、外部ライブラリとの接続を選ぶ | 未決定 |
| STO-02 | SPI SD のファイル利用 | USB MSC 用の SD ブロックアクセスあり | スケッチからの SD ファイル読み書き。既存 MSC ストレージとは役割を区別 | 未決定 |
| STO-03 | SDIO／SDMMC によるストレージ | SDIO は現行対応表では対象外 | 搭載系列の SD カード制御とブロック API、STO-01 との接続 | 未決定 |
| STO-04 | Flash 上のファイルシステム | 汎用 API なし | 内蔵／外付け Flash、LittleFS 等との接続。SYS-01 の少量設定保存とは別判断 | 未決定 |
| STO-05 | USB Host MSC のファイル利用 | Host MSC はブロック読み書きまで | マウント・ファイル操作、STO-01 との接続 | 未決定 |
| NET-01 | Ethernet | 内蔵 MAC のドライバ（H417・V307 は実機、V407 / V467・V317・V208 はビルドのみ） | 外付け SPI Ethernet | 一部採用 |
| NET-02 | TCP/IP とネットワーク共通 API | Network ライブラリ（lwIP 2.2.1、ESP32 の `NetworkClient` / `NetworkServer` / `NetworkUDP`、DHCP、DNS）。Ethernet と USB NCM の共通化（2026-10-21） | — | 採用 |
| NET-03 | TLS・ネットワーク上位機能 | TLS（NetworkClientSecure、Mbed TLS 4.1）、mDNS、SNTP の `configTime()` は入った | HTTP Client／Server、MQTT。同梱範囲 | 一部採用 |
| UPD-01 | ファームウェア自己更新 | ISP への移行入口はあるが、Update 相当の API なし。土台の `ch32rv_flash.h` は入った | イメージ書き込み・検証・起動切り替え。bootloader の責務を決める | UF2（MscFirmwareDisk）は実装する（2026-10-20）。ほかは未決定 |
| UPD-02 | 更新データの受信経路 | 実際の DFU download・MSC 更新クラス等はない | USB DFU／MSC、UART、SD、ネットワーク更新を個別に判断。UPD-01 と分離 | UF2 は実装する（2026-10-20）。ほかは未決定 |

MSC デバイスを PC に見せる機能と、スケッチがファイルを開く機能は別。
USB NCM で Ethernet フレームを送受信できることと、TCP/IP を使えることも別。
DFU Runtime による工場 ISP への移行は、DFU download や自己更新の実装とは区別する。

## USB の拡張

| ID | 検討する機能 | 現状 | 追加範囲と判断点 | 採否 |
|---|---|---|---|---|
| USB-01 | Device の未対応系列・コントローラ | 自作のスタックで全系列のドライバ（V103、V20x の USBFS と USBD、V30x の USBFS と USBHS、第2世代 USBHS、X315 の USBSS）。実機は X035 と H417、ほかはビルドのみ | 各系列の実機確認 | 採用 |
| USB-02 | Host のコントローラ・速度拡張 | 現行 Host は対応型番の USBFS | USBHS Host 等。USB-01 の Device ドライバとは別に採否を決める | 未決定 |
| USB-03 | USB SuperSpeed Device | H417 の USBSS のデバイス（実機で列挙・CDC・DFU）、X315 はビルドのみ。速度ごとの記述子あり | SS のホスト（TODO） | 採用 |
| USB-04 | USB SuperSpeed Host | 未実装。設計検討では Device と別段階 | SS Host ドライバ、列挙・転送、HS 機器との接続管理 | 未決定 |
| USB-05 | Host Audio／Video | 現行 Host API にはない | アイソクロナス転送、UAC／UVC クラス。両クラスと対応速度を個別に判断 | 未決定 |
| USB-06 | Host ネットワーク | 現行 Host API にはない | USB ネットワーク機器のフレーム送受信、NET-02 との接続 | 未決定 |
| USB-07 | Vendor 直接転送 | `writeDirect()` / `onRxData()` / `setRxDataBuffer()`（2026-10-21、84b57b0） | DMA の範囲 | 採用 |
| USB-08 | ISP 移行の系列拡張 | `enterBootloader()` は X033／X035、X305 / X315（ビルドのみ）。H41x は試して失敗。ほかの系列は BOOT0 の端子だけ | — | 一部採用 |
| USB-09 | ソフトウェア USB Device | V003 アプリ側では未実装。既存 TODO あり | rv003usb 等の採否、既存 Device API と共通化する範囲、小容量向けクラス構成 | 未決定 |
| PD-01 | USB PD の系列拡張 | L103 / M103、V205、X305 / X315、H41x、M030 の USBPD0（2026-10-22、ビルドのみ） | 8 MHz の系列での BMC、M030 の USBPD1、実機確認 | 一部採用 |
| PD-02 | USB PD DRP・Role Swap | Sink／Source の選択はあるが、公開された DRP／Role Swap 機能なし | DRP、Power Role／Data Role／VCONN Swap を個別に判断 | 未決定 |
| PD-03 | USB PD の電源プロファイル拡張 | 固定電圧／PPS に対応。Battery／Variable は Sink で列挙のみ、EPR は対象外 | Battery／Variable の要求、EPR／AVS 等を個別に判断 | 未決定 |

USB Device の Audio／Video クラスは既存。実際の音源・マイク・カメラとの接続には、
I2S、ADC、DAC、DVP 等の別機能が必要になる。
USB SS の既存検討は [usb-superspeed-design.ja.md](docs/usb-superspeed-design.ja.md) を参照する。

## 現行対応表で対象外の専用周辺機能

| ID | 検討する機能 | 現状 | 追加範囲と判断点 | 採否 |
|---|---|---|---|---|
| HW-01 | DMA | Arduino 公開 API としては対象外 | SPI／ADC／I2S 等の内部利用と、汎用 DMA・資源管理 API の公開を別々に判断。DMA クラスは必須ではない | 未決定 |
| HW-02 | CRC | 対象外 | ハードウェア CRC の利用 API。対応方式とソフトウェア処理との役割分担 | 未決定 |
| HW-03 | RNG | 対象外。`random()` はソフトウェア実装 | ハードウェア乱数取得。通常の疑似乱数 API と用途・保証範囲を区別 | 未決定 |
| HW-04 | PIOC・パルス列送受信 | PIOC は対象外 | プログラマブル IO、パルス列・独自通信の送受信。ESP32 RMT に似た用途をどの資源で扱うか | 未決定 |
| HW-05 | FSMC | 対象外 | 外部メモリ・パラレルバス機器への接続。外付け機器のドライバとは分離 | 未決定 |
| HW-06 | QSPI | 対象外 | 外付け Flash 等への複数線転送。通常 SPI と別に公開するか | 未決定 |
| HW-07 | PSRAM | 対象外 | 初期化・利用領域・メモリ確保 API。通常 heap に統合するか専用領域にするか | 未決定 |
| HW-08 | I3C | 対象外 | Controller／Target、列挙・転送等。ハードウェア能力に応じてモード別に判断 | 未決定 |
| HW-09 | DVP カメラ入力 | 対象外 | フレーム取得、バッファ、完了通知、USB Video への接続 | 未決定 |
| HW-10 | LTDC 表示出力 | 対象外 | 表示タイミング、フレームバッファ、描画ライブラリとの接続 | 未決定 |
| HW-11 | ARGB LED 出力 | 対象外 | 搭載ハードウェアの LED 駆動 API。HW-04 による実現と別に判断 | 未決定 |

## プロジェクトの対象範囲として判断する項目

以下も現在の対象外方針を理由に候補から除外しない。ただし、個別の周辺クラス追加より広い変更になる。

| ID | 検討する範囲 | 現状 | 判断点 | 採否 |
|---|---|---|---|---|
| SCOPE-01 | BLE・無線 SoC 機能 | V208 の BLE、CH32V208W・CH58x・CH59x 等の無線 stack が必要な機能は対象外 | 無線 stack、対応チップ、ライセンス・容量。既存 CH32 の周辺 API と無線 SoC 自体の追加を分ける | 未決定 |
| SCOPE-02 | RTOS | コアは bare metal。FreeRTOS／RT-Thread／HarmonyOS／TencentOS は同梱対象外 | コアへの組み込み、独立 library／port、連携用 API を分け、OS ごとに採否を判断 | 未決定 |
| SCOPE-03 | 汎用センサ・表示器ドライバの同梱 | 同梱対象外 | 外部 Arduino ライブラリの利用を基本にするか、一部を同梱するか。FS は STO-01〜04 で個別判断 | 未決定 |
| SCOPE-04 | WCH EVT／SPL 互換 API | 対象外 | Arduino API とは別にベンダ API 互換層を提供するか。系列差・依存更新の責務 | 未決定 |
| SCOPE-05 | Arm 系 CH32F | 対象外 | RISC-V コアとは別アーキテクチャの platform を扱うか。クラス追加とは別の対象チップ判断 | 未決定 |
| SCOPE-06 | ベンダ IDE とのバイナリ互換 | 対象外 | startup、ABI、linker、runtime の互換性を提供範囲に含めるか | 未決定 |

## 判断時に参照する現行仕様

- [プロジェクトの対象](docs/project-scope.ja.md)
- [ペリフェラル対応表](docs/peripheral-support.ja.md)
- [同梱ライブラリの範囲](docs/adr/0013-bundled-libraries.ja.md)
- [RTOS 方針](docs/adr/0006-rtos-policy.ja.md)
- [タイマー資源の契約](docs/timer-design.ja.md)
- [ソフトウェア実装ペリフェラル](docs/software-peripherals.ja.md)
- [USB の仕様と今後の予定](docs/usb.ja.md)
- [既存 TODO](TODO.ja.md)

## ESP32 の比較参照

ESP32 の公開 API を機能区分の参考にしたもの。CH32 側に同じハードウェアがあることを意味しない。

- [機能とペリフェラル](https://docs.espressif.com/projects/arduino-esp32/en/latest/libraries.html)
- [SPIClass](https://github.com/espressif/arduino-esp32/blob/master/libraries/SPI/src/SPI.h)
- [UART](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/serial.html)
- [ADC](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/adc.html)
- [Timer](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/timer.html)
- [Preferences](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html)
- [Network](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/network.html)
- [FS と File](https://github.com/espressif/arduino-esp32/blob/master/libraries/FS/src/FS.h)
- [Update](https://github.com/espressif/arduino-esp32/blob/master/libraries/Update/src/Update.h)
