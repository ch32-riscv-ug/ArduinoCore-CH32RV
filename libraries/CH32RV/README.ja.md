# CH32RV

ここには core API の examples、レジスタレベルの逃げ道、チップ全体を扱う `CH32RV` オブジェクトが入っています。

## `CH32` オブジェクト (CH32RVSystem.h)

ESP core の `ESP.*` と同じ役割で、Arduino 標準 API がない chip 全体の機能をまとめます。

```cpp
#include <CH32RV.h>

CH32RV.restart();                        // ソフトウェアリセット(戻らない)
CH32RV.resetReason();                    // CH32RV_RESET_SOFTWARE / _WATCHDOG / ...
CH32RV.resetReasonName();                // "software" など、印字用
CH32RV.wdtEnable(2000);                  // 2秒ごとに餌をやらないとリセット
CH32RV.wdtFeed();                        // loop()で呼ぶ
CH32RV.getVddMillivolts();               // 電源電圧(mV)、ADCの内部基準から
analogReadTemp();                        // チップの温度(℃)、センサが無ければNAN (core)
analogReadVref();                        // 内部基準をanalogRead()と同じ幅で (core)
```

注意:

- **`wdtDisable()`はありません**。IWDGは一度動くと止められない石なので、
  止まらない`disable()`を置くくらいなら無いほうが正直です
- **timeoutは近似です**。時計のLSIは仕様上±50%近くばらつきます。
  換算は「実timeoutが頼んだ値より短くなる」側に倒してあります
- 対応しない部品では`wdtEnable()`が**falseを返します**——
  CH32M030(IWDGが無い)。CH32X033/X035はLSIが無く、IWDGはHSI/1024(46.875 kHz)で動きます
- **電圧と温度は典型値からの計算です**。内部基準(Vrefint)はデータシートの典型値で、
  ばらつきは数%。温度は校正していないので、系列によっては数十℃ずれます
  (V103はデータシートのV25が1.1〜1.6 Vと広い)。温度センサがあるのは
  V103・V20x・V205・V30x・V407/V467・L103/M103。M030は内部基準のデータが無く0を返します

同居している理由は、Arduinoのプラットフォームが**ライブラリ経由でしかexamplesを
配れない**ことと、ライブラリには最低1つヘッダが要ることです。
他のコアがこの用途に空のダミーを置いているのに対し、
こちらはそのファイルに仕事を持たせました。

## examples

`digitalWrite()`・`analogRead()`・`tone()`などはcoreのもので、
どのライブラリにも属さないので、examplesはここに置いています。すべて共通で:

- pinはpad名(`PC0`・`PA1`)かvariantの名前(`A0`・`SDA`・`SCK`…)で書きます。
  数値は書きません。exampleが選んだpadは**exampleの都合であって変更前提**です
  — Generic boardはsiliconのseriesであって基板ではないので`LED_BUILTIN`を
  定義せず、coreが勝手に決めることもしません(`docs/board-layer-rules.ja.md`)
- 冒頭コメントに**何を示すか**と**必要な配線**を書いています

| example | 何を示すか |
|---|---|
| Blink | `pinMode`・`digitalWrite`・`delay` |
| SerialEcho | `Serial`の入出力 |
| AnalogRead | `analogRead` |
| Fade | `analogWrite` |
| ToneMelody | `tone`・`noTone` |
| PinInterrupt | `attachInterrupt`とISRとの状態共有 |
| ShiftOut | 74HC595への`shiftOut` |
| PulseIn | `pulseIn`・`delayMicroseconds` |
| Timing | `millis`・`micros`と、その巻き戻り |
| RandomNumbers | `random`・`randomSeed` |
| AnalogResolution | `analogReadResolution`・`analogWriteResolution` |
| CriticalSection | `interrupts`・`noInterrupts`・`volatile` |
| PrintFormatting | `Print`・`String`・`dtostrf` |
| PinCapabilities | このチップに実在するpad |

**知らないboardで最初に走らせるべきはPinCapabilities**です。
CH32のpin番号は`((port + 2) << 5) | bit`なので飛び飛びで、
PA0が64、PB0が96、その間のほとんどの番号はpadに対応しません(0〜63は製品boardの印刷番号用)。
このsketchは推測せずvariantに問い合わせます。

### 製品ボードのテンプレート

製品ボードごとに、その板が出しているペリフェラルを全部初期化して使う雛形です。板のpinをすべて名前付きで書いて
あり、要らないものを消して使い始めます。同時に使えないものはコメントで外してあり、何と引き換えになるかを書いて
います。その板（fqbn）だけでビルドします（`board:` 宣言、`docs/examples-build-rules.ja.md`）。

| example | 板 | 初期化するもの |
|---|---|---|
| BoardWeActCH32X035 | WeAct CH32X035 Core Board（CH32X035F8U6） | USB（`SerialUSB` + 書き込み用の DFU ランタイム）、USB PD（5 V）、UART 2系統、SPI、ADC 2本、PWM、LED（PB12）。I2C は USB か SWD の端子にしか出ていないのでコメントで外してある |
| BoardUIAPduinoProMicro | UIAPduino Pro Micro CH32V003 V1.4 | UART、I2C（スキャン）、SPI、ADC 3本、PWM、LED（PC0）。SPI はボードメニューの AUTO のまま。メーカーの配置図にある NSS（PC1 / PC0）は I2C と LED に重なるので、CS は PC3 にしてコメントで示す |

どちらも実機で動作を確かめています（2026-10-14）。

## 逃げ道

```cpp
#include <CH32RV.h>

CH32RV_TIM_ATRLR(CH32RV_TIM2_BASE) = 999;   // timerを直接叩く
```

`CH32RV.h`はレジスタマップ・GPIOヘルパ・pinエンコード・route表をまとめて読み込みます。
注意書きが2つあります。

1. **この層は安定APIではありません。** 名前はベンダSDKのものではなく
   このコア独自のもので、互換性の対象外です。
   これを使うsketchは、Arduino APIだけを使うsketchと違って
   **使用したコアの版に固定**されます。
2. **coreも同じレジスタを使っています。** `Serial`や`Wire`が開いている状態で
   `AFIO_PCFR1`を直接書くと`begin()`や`setRoute()`と衝突します。
   症状は「次に開いたときにpinが動く」です。
