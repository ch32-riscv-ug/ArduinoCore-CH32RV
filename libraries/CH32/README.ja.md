# CH32

ここには3つのものが入っています。**coreのAPIのexamples**、
**レジスタレベルの逃げ道**、そして**チップ全体を扱う`CH32`オブジェクト**です。

## `CH32` オブジェクト (CH32System.h)

ESPコアの`ESP.*`と同じ役どころです。Arduino標準APIが無い機能を、
ESP32の流儀に寄せて置いています(方針は
[R-27](../../docs/research/system-api-esp32-style.ja.md))。

```cpp
#include <CH32.h>

CH32.restart();                        // ソフトウェアリセット(戻らない)
CH32.resetReason();                    // CH32_RESET_SOFTWARE / _WATCHDOG / ...
CH32.resetReasonName();                // "software" など、印字用
CH32.wdtEnable(2000);                  // 2秒ごとに餌をやらないとリセット
CH32.wdtFeed();                        // loop()で呼ぶ
```

3つだけ注意:

- **`wdtDisable()`はありません**。IWDGは一度動くと止められない石なので、
  止まらない`disable()`を置くくらいなら無いほうが正直です
- **timeoutは近似です**。時計のLSIは仕様上±50%近くばらつきます。
  換算は「実timeoutが頼んだ値より短くなる」側に倒してあります
- 対応しない部品では`wdtEnable()`が**falseを返します**——
  CH32M030(IWDGが無い)、CH32X033/X035(LSI周波数のデータが未着、依頼中)

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
- 一番広いboardと一番狭いboardの2つでCIがコンパイルします

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
CH32のpin番号は`(port << 5) | bit`なので飛び飛びで、
PA0が0、PB0が32、その間のほとんどの番号はpadに対応しません。
このsketchは推測せずvariantに問い合わせます。

## 逃げ道

```cpp
#include <CH32.h>

CH32_TIM_ATRLR(CH32_TIM2_BASE) = 999;   // timerを直接叩く
```

`CH32.h`はレジスタマップ・GPIOヘルパ・pinエンコード・route表をまとめて読み込みます。
注意書きが2つあります。

1. **この層は安定ではありません。** 名前はベンダSDKのものではなく
   このコア独自のもので、**変わる予定**です
   (レジスタマップは手書きから`ch32-device-data`生成へ移行していきます)。
   これを使うsketchは、Arduino APIだけを使うsketchと違って
   **コアのバージョンに固定**されます。
2. **coreも同じレジスタを使っています。** `Serial`や`Wire`が開いている状態で
   `AFIO_PCFR1`を直接書くと`begin()`や`setRoute()`と衝突します。
   症状は「次に開いたときにpinが動く」です。
