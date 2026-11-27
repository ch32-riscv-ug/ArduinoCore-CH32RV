# 基本サンプルの使い方

[English](README.md)

GPIO、UART、ADC、PWM、割り込み等の小さな例です。各フォルダの README で配線・操作・API の役割・改造時の注意を説明します。

## 最初の準備

Arduino IDE の「ファイル → スケッチ例 → CH32RV」から開き、ボードと正確な Part Number を選びます。
Generic board は基板の配線を決めないため、`.ino` 冒頭の端子定数を回路図へ合わせます。
`A1` や `RX` の実端子を調べるときは PinCapabilities から始めます。

多くの例は `Serial` の UART へログを出します。アダプタの RX←ボード TX、TX→ボード RX、GND 同士を接続し、
I/O 電圧と `Serial.begin()` の通信条件を合わせます。USB コネクタがあるだけでは、この UART ログを読めるとは限りません。

CLI は core 導入済みの環境で、リポジトリのルートからビルドできます。

```sh
arduino-cli compile --fqbn ch32-riscv-ug:ch32rv:CH32X035:part=CH32X035F8U6 \
  libraries/CH32RV/examples/01.Basics/Timing
```

Blink の `LED_BUILTIN` は基板に合わせて明示します。製品 board が定義する場合はその定義を使い、
Generic board ではスケッチへ定義を加えるか、実際の端子名をビルドオプションで渡します。

## 目的から選ぶ

### 01.Basics（基本）

| 例 | 学ぶこと |
|---|---|
| [Blink](01.Basics/Blink/README.ja.md) | GPIO を High/Low に切り替え、スケッチが実行されることを LED で確認します。 |
| [Fade](01.Basics/Fade/README.ja.md) | PWM の duty を変えて LED の明るさを連続的に変えます。 |
| [PrintFormatting](01.Basics/PrintFormatting/README.ja.md) | 整数・小数・文字列の表示と、生バイト送信の違いを確かめます。 |
| [RandomNumbers](01.Basics/RandomNumbers/README.ja.md) | 同じ seed による疑似乱数の再現と、`ch32rv_random()` を seed にした場合の違いを確かめます。 |
| [ShiftOut](01.Basics/ShiftOut/README.ja.md) | シフトレジスタへ1 byteずつ送り、出力を順番に切り替えます。 |
| [Timing](01.Basics/Timing/README.ja.md) | `millis()`・`micros()` と、桁あふれをまたげる時刻の引き算を示します。 |
| [ToneMelody](01.Basics/ToneMelody/README.ja.md) | 周波数と長さを指定して受動ブザーで音階を鳴らします。 |
| [ButtonDebounce](01.Basics/ButtonDebounce/README.ja.md) | 待ち続けずにチャタリングを除き、新しい押下を一度ずつ数えます。 |
| [AnalogInputToPwm](01.Basics/AnalogInputToPwm/README.ja.md) | 可変抵抗の読み取りを、ADCとPWMの範囲を明示してdutyへ変換します。 |

### 02.GPIO（端子・割り込み）

| 例 | 学ぶこと |
|---|---|
| [PinInterrupt](02.GPIO/PinInterrupt/README.ja.md) | GPIO のエッジを割り込みで数え、表示を `loop()` へ分けます。 |
| [PinCapabilities](02.GPIO/PinCapabilities/README.ja.md) | variant が定義する端子の有無と ADC の対応を調べます。 |
| [PulseIn](02.GPIO/PulseIn/README.ja.md) | トリガーを出力し、入力パルスの High の継続時間を測ります。 |
| [RgbLed](02.GPIO/RgbLed/README.ja.md) | `rgbLedWrite()` で WS2812 / SK6812 の RGB LED をどの端子でも光らせます。 |
| [TouchRead](02.GPIO/TouchRead/README.ja.md) | `touchRead()` で TouchKey のパッドを読み、指が触れたら知らせます（V00x・X035・V20x・V30x）。 |

### 03.Analog（ADC・PWM）

| 例 | 学ぶこと |
|---|---|
| [AnalogRead](03.Analog/AnalogRead/README.ja.md) | アナログ入力の生値と mV 単位の電圧を UART で観察します。 |
| [AnalogContinuous](03.Analog/AnalogContinuous/README.ja.md) | arduino-esp32 の連続 ADC の API（`analogContinuous()`）で、ADC が自分で埋めるフレームごとに平均します。 |
| [InternalAnalogRead](03.Analog/InternalAnalogRead/README.ja.md) | 内部の基準電圧・電源電圧・チップの温度を読みます。 |
| [AnalogResolution](03.Analog/AnalogResolution/README.ja.md) | ADC と PWM の数値のスケールを変える使い方を示します。 |
| [PwmFrequency](03.Analog/PwmFrequency/README.ja.md) | dutyの数値を保ってPWM周波数を切り替えます。 |

### 04.Serial（UART）

| 例 | 学ぶこと |
|---|---|
| [SerialEcho](04.Serial/SerialEcho/README.ja.md) | ハードウェア UART で行を受け取り、前後の空白を除いて返信します。 |
| [SerialBridge](04.Serial/SerialBridge/README.ja.md) | `Serial` と `Serial1` の間でバイトを中継し、PC から UART のモジュール（GPS・Wi-Fi など）を直接操作します。 |
| [SerialLineReceiver](04.Serial/SerialLineReceiver/README.ja.md) | 文字の到着を待たずに行を組み立て、長さ超過とtimeoutを扱います。 |
| [SerialPacketReceiver](04.Serial/SerialPacketReceiver/README.ja.md) | 長さ付きバイナリパケットをchecksum・timeout付きで受信します。 |
| [SerialReceiveEvent](04.Serial/SerialReceiveEvent/README.ja.md) | 線が静かになったらまとまりを受け取り（`onReceive()`）、受信の誤りも知らせてもらいます。 |
| [SerialFlowControl](04.Serial/SerialFlowControl/README.ja.md) | RTS で送り手を待たせ、CTS で送信を止めます（`setHwFlowCtrlMode()`）。 |
| [SerialMultiprocessor](04.Serial/SerialMultiprocessor/README.ja.md) | 1 本の UART の線で何台ものボードをアドレスで呼び分けます（`setAddress()`）。 |
| [SerialRS485](04.Serial/SerialRS485/README.ja.md) | RS485 のバスに問い合わせます。送信のたびにコアがトランシーバーの DE を切り替えます。 |
| [SerialHalfDuplex](04.Serial/SerialHalfDuplex/README.ja.md) | 開放ドレインの 1 本の線で送受信します（`setPins(p, p)`）。 |
| [SerialDmaBuffers](04.Serial/SerialDmaBuffers/README.ja.md) | `loop()` がほかの仕事をしている間も 1 Mbaud の流れを受け取ります（DMA が運ぶ大きいバッファ、`setRxBufferSize()`）。 |

### 05.Timer（タイマー・割り込み）

| 例 | 学ぶこと |
|---|---|
| [CriticalSection](05.Timer/CriticalSection/README.ja.md) | 割り込みと共有する値を、短い割り込み禁止区間でコピーする書き方を示します。 |
| [TimerAlarm](05.Timer/TimerAlarm/README.ja.md) | arduino-esp32 のタイマー API（`timerBegin()`・`timerAlarm()`）で、毎秒関数を動かします。 |
| [InterruptPriority](05.Timer/InterruptPriority/README.ja.md) | 遅いタイマー割り込みを下げ、その処理中も `millis()` を進めます。 |
| [TimerResource](05.Timer/TimerResource/README.ja.md) | タイマーを借り、基準周期と更新割り込みを設定するライブラリ実装者向けの例です。 |

### 06.System（システム・保存）

| 例 | 学ぶこと |
|---|---|
| [LowVoltageDetect](06.System/LowVoltageDetect/README.ja.md) | 電源が閾値を下回ったら知らせ（PVDのEXTIの線）、線をソフトウェアから起こしてもみます。 |
| [Crc32](06.System/Crc32/README.ja.md) | `CH32RV.crc32()` でデータを検査します（1 回でも分けても）。 |
| [SystemInfo](06.System/SystemInfo/README.ja.md) | ビルド対象・メモリ・利用可能なチップ固有情報を表示します。 |
| [ResetReason](06.System/ResetReason/README.ja.md) | リセット理由と、操作によるソフトウェア再起動を比較します。 |
| [WatchdogRecovery](06.System/WatchdogRecovery/README.ja.md) | 操作でwatchdogを開始し、給餌停止による再起動を確認します。 |
| [WindowWatchdog](06.System/WindowWatchdog/README.ja.md) | 操作で窓ウォッチドッグを開始し、遅い餌と早い餌でのリセットを確認します。 |
| [RtcAlarm](06.System/RtcAlarm/README.ja.md) | RTCで日時を保ち、アラームを鳴らし、アラームまで眠り、起動の回数をバックアップレジスタで数えます。 |
| [FlashSettings](06.System/FlashSettings/README.ja.md) | 内蔵のフラッシュの 1 ページに設定を保存し、リセット後に読み戻します（上級者向け。推奨は外部の SD・EEPROM）。 |
| [FlashSettingsTwoBanks](06.System/FlashSettingsTwoBanks/README.ja.md) | 2 ページに交互に保存し、保存中の電源断でも前の設定を残します（上級者向け）。 |

### 07.Chips（特定の石だけ）

| 例 | 学ぶこと |
|---|---|
| [DualCoreV3F](07.Chips/CH32H417/DualCoreV3F/README.ja.md) | CH32H417: スケッチを V5F で動かしたまま、`setup1()` / `loop1()` を V3F で動かします。 |
| [DualCoreMessages](07.Chips/CH32H417/DualCoreMessages/README.ja.md) | CH32H417: V5F と V3F がメッセージ（IPC）とハードウェアのセマフォ（HSEM）でやり取りします。 |

### 08.Boards（特定のボード）

| 例 | 学ぶこと |
|---|---|
| [WeActCH32X035](08.Boards/WeActCH32X035/README.ja.md) | WeAct CH32X035 Core Board のいちばん小さい出発点です。LED を点滅させ、USB-C の USB シリアルへ出します（USB だけで書き込みと出力）。 |
| [WeActCH32X035Peripherals](08.Boards/WeActCH32X035Peripherals/README.ja.md) | 同じ基板で USB・PD・UART 2 本・SPI・ADC・PWM をまとめて使います。 |
| [WeActCH32X035OledSH1106](08.Boards/WeActCH32X035OledSH1106/README.ja.md) | WeAct CH32X035 の PC18 / PC19 に SH1106 の OLED をつなぎ、U8g2 で描きます（デバッグの端子を手放し、書き込みは USB）。 |
| [UIAPduinoProMicro](08.Boards/UIAPduinoProMicro/README.ja.md) | UIAPduino Pro Micro の LED を、印刷の番号で点滅させます（USB だけで動く）。端子の対応と、Serial を見るための配線も説明します。 |

一つずつ動作を確認してから組み合わせます。`delay()` で待つ例や、見せるためにログを多く出す例は、
多数の機能を同時に動かす最終形ではありません。[スケッチの組み立て方](../../../docs/sketch-guide.ja.md) で
時刻管理・割り込みからの受け渡し・callback の扱いへ進んでください。

全ライブラリの入口は [サンプル案内](../../../docs/examples.ja.md)、ビルド設定は
[examples のビルド規則](../../../docs/examples-build-rules.ja.md) を参照してください。
