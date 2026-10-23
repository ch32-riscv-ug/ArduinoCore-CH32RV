# 基本サンプルの使い方

GPIO、UART、ADC、PWM、割り込み等の小さな例です。各フォルダの README で配線・操作・API の役割・改造時の注意を説明します。

## 最初の準備

Arduino IDE の「ファイル → スケッチ例 → CH32RV」から開き、ボードと正確な Part Number を選びます。
Generic board は基板の配線を決めないため、`.ino` 冒頭の端子定数を回路図へ合わせます。
`A1` や `RX` の実端子を調べるときは PinCapabilities から始めます。

多くの例は `Serial` の UART へログを出します。アダプタの RX←ボード TX、TX→ボード RX、GND 同士を接続し、
I/O 電圧と `Serial.begin()` の通信条件を合わせます。USB コネクタがあるだけでは、この UART ログを読めるとは限りません。

CLI は core 導入済みの環境で、リポジトリのルートからビルドできます。

```sh
arduino-cli compile --fqbn ch32-riscv-ug:ch32rv:CH32X035:pnum=CH32X035F8U6 \
  libraries/CH32RV/examples/Timing
```

Blink の `LED_BUILTIN` は基板に合わせて明示します。製品 board が定義する場合はその定義を使い、
Generic board ではスケッチへ定義を加えるか、実際の端子名をビルドオプションで渡します。

## 目的から選ぶ

| 例 | 学ぶこと |
|---|---|
| [Blink](Blink/README.ja.md) | GPIO を High/Low に切り替え、スケッチが実行されることを LED で確認します。 |
| [SerialEcho](SerialEcho/README.ja.md) | ハードウェア UART で行を受け取り、前後の空白を除いて返信します。 |
| [AnalogRead](AnalogRead/README.ja.md) | アナログ入力の生値を UART で観察します。 |
| [AnalogResolution](AnalogResolution/README.ja.md) | ADC と PWM の数値のスケールを変える使い方を示します。 |
| [Fade](Fade/README.ja.md) | PWM の duty を変えて LED の明るさを連続的に変えます。 |
| [PinInterrupt](PinInterrupt/README.ja.md) | GPIO のエッジを割り込みで数え、表示を `loop()` へ分けます。 |
| [CriticalSection](CriticalSection/README.ja.md) | 割り込みと共有する値を、短い割り込み禁止区間でコピーする書き方を示します。 |
| [PinCapabilities](PinCapabilities/README.ja.md) | variant が定義する端子の有無と ADC の対応を調べます。 |
| [PrintFormatting](PrintFormatting/README.ja.md) | 整数・小数・文字列の表示と、生バイト送信の違いを確かめます。 |
| [PulseIn](PulseIn/README.ja.md) | トリガーを出力し、入力パルスの High の継続時間を測ります。 |
| [RandomNumbers](RandomNumbers/README.ja.md) | 同じ seed による疑似乱数の再現と、seed を変えた場合の違いを確かめます。 |
| [ShiftOut](ShiftOut/README.ja.md) | シフトレジスタへ1 byteずつ送り、出力を順番に切り替えます。 |
| [Timing](Timing/README.ja.md) | `millis()`・`micros()` と、桁あふれをまたげる時刻の引き算を示します。 |
| [ToneMelody](ToneMelody/README.ja.md) | 周波数と長さを指定して受動ブザーで音階を鳴らします。 |
| [TimerResource](TimerResource/README.ja.md) | タイマーを借り、基準周期と更新割り込みを設定するライブラリ実装者向けの例です。 |
| [BoardWeActCH32X035](BoardWeActCH32X035/README.ja.md) | WeAct CH32X035 Core Board の端子名を明示して、各周辺機能を組み合わせる出発点です。 |
| [BoardUIAPduinoProMicro](BoardUIAPduinoProMicro/README.ja.md) | UIAPduino Pro Micro の基板上の端子名を使って、UART・I2C・SPI・ADC・PWM を初期化します。 |

一つずつ動作を確認してから組み合わせます。`delay()` で待つ例や、見せるためにログを多く出す例は、
多数の機能を同時に動かす最終形ではありません。[スケッチの組み立て方](../../../docs/sketch-guide.ja.md) で
時刻管理・割り込みからの受け渡し・callback の扱いへ進んでください。

全ライブラリの入口は [サンプル案内](../../../docs/examples.ja.md)、ビルド設定は
[examples のビルド規則](../../../docs/examples-build-rules.ja.md) を参照してください。
