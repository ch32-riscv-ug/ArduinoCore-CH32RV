# CH32RV

ここには core API の examples、レジスタレベルの逃げ道、チップ全体を扱う `CH32RV` オブジェクトが入っています。

## `CH32RV` オブジェクト (CH32RVSystem.h)

ESP core の `ESP.*` と同じ役割で、Arduino 標準 API がない chip 全体の機能をまとめます。

```cpp
#include <CH32RV.h>

CH32RV.restart();                        // ソフトウェアリセット(戻らない)
CH32RV.resetReason();                    // CH32RV_RESET_SOFTWARE / _WATCHDOG / _PANIC（Stack Check がスタックのあふれでリセットした）...
CH32RV.resetReasonName();                // "software" など、印字用
CH32RV.wdtEnable(2000);                  // 2秒ごとに餌をやらないとリセット
CH32RV.wdtFeed();                        // loop()で呼ぶ
CH32RV.clockSource();                    // CH32RV_CLOCK_SOURCE_HSE / _HSI（Clock Source メニュー）
CH32RV.getVddMillivolts();               // 電源電圧(mV)、ADCの内部基準から
CH32RV.getHeapSize();                    // ヒープの大きさと、malloc() がまだ渡せる量
CH32RV.getFreeHeap();
CH32RV.getStackSize();                   // スタックの確保（メニュー Stack Size）と、いちばん深く使ったときの残り
CH32RV.getMinFreeStack();                // （残りはメニュー Stack Check が On のときだけ。Off では 0）
CH32RV.getChipModel();                   // "CH32V003": ビルドした系列
CH32RV.getSketchSize();                  // スケッチが使う flash と、その部品の残り
CH32RV.getFreeSketchSpace();
CH32RV.getFlashChipSize();               // シリコンの flash の大きさ(電子署名から)
CH32RV.getUniqueId();                    // チップの固有 ID の 64 ビット
analogReadTemp();                        // チップの温度(℃)、センサが無ければNAN (core)
analogReadVref();                        // 内部基準をanalogRead()と同じ幅で (core)
```

注意:

- **`wdtDisable()`はありません**。IWDGは一度動くと止められない石なので、
  止まらない`disable()`を置くくらいなら無いほうが正直です
- **電源投入から動かすこともできます**。ツール → Watchdog の`Running from power-on`を選ぶと、起動時にコアが
  オプションバイト(USER の IWDG_SW)を書き換え、IWDGは電源投入から既定の周期(V003で約128 ms)で動きます。
  `setup()`の早いうちに`wdtEnable()`で周期を決め、`loop()`で`wdtFeed()`を呼んでください。
  工場出荷の項目は何もせず、チップの設定のままです。戻すときは、工場出荷の項目でビルドしたスケッチを書いてから
  `ch32rv target option set iwdg=sw --yes`を実行し、リセットします(`Running from power-on`のスケッチが
  残っていると、起動のたびにUSERをhwへ書き戻します)。
  メニューがあるのはV00x・M007・X033/X035・X305/X315・V20x・V205・V30x・L103/M103・V407/V467です
- **timeoutは近似です**。時計のLSIは仕様上±50%近くばらつきます。
  換算は「実timeoutが頼んだ値より短くなる」側に倒してあります
- 対応しない部品では`wdtEnable()`が**falseを返します**——
  CH32M030(IWDGが無い)。CH32X033/X035はLSIが無く、IWDGはHSI/1024(46.875 kHz)で動きます
- **電圧と温度は典型値からの計算です**。内部基準(Vrefint)はデータシートの典型値で、
  ばらつきは数%。温度は校正していないので、系列によっては数十℃ずれます
  (V103はデータシートのV25が1.1〜1.6 Vと広い)。温度センサがあるのは
  V103・V20x・V205・V30x・V407/V467・L103/M103。M030は内部基準のデータが無く0を返します
- **`clockSource()`** が HSE を返すのは、Clock Source メニューで水晶を選び、その水晶が立ったときだけです。
  立たなければ同じ CPU Clock を HSI で作ります(`F_CPU` は同じで、精度は HSI のもの)
- **`getUniqueId()`** は UNIID1 | UNIID2 << 32 です。UID の 96 ビットのうち、チップごとに違うのはこの 64 ビットだけです。
  MAC アドレスではないので、ESP32 の `getEfuseMac()` の名前にはしていません
- **H41x** には、高速の端子の電源(VIO18)を変える `setVio18Millivolts()` / `getVio18Millivolts()` があります
  (1200、1800、2500、3300 mV)。VIO18 Supply メニューを使うと `setup()` の前に決まります

### 休止（`lightSleep()` / `deepSleep()`）

ESP32 の `esp_sleep_*` と同じ意味を、`CH32RV` の名前で出しています。

```cpp
CH32RV.enableTimerWakeup(5 * 1000000ULL);  // 5秒後に起きる
CH32RV.enablePinWakeup(PA1, LOW);          // または PA1 が LOW になったら（端子のモードは自分で）
CH32RV.lightSleep();                       // 起きたら戻る。RAMと端子の状態は残る
CH32RV.deepSleep();                        // 起きたら setup() から（ソフトで再起動）
CH32RV.wakeupCause();                      // CH32RV_WAKEUP_TIMER / _PIN / _UNDEFINED
CH32RV.timeDomain();                       // 測れない時間を飛ばすたびに増える
```

- **対応は V003・V00x・X033/X035**（AWU で起こす）と **V20x・V205・V30x・L103/M103**（RTC のアラームで起こす。ストップで眠る）。ほかの系列は `CH32RV_SLEEP_UNSUPPORTED` を返します。
- **RTC の系列では `time()` が RTC になります。** LSI を測って 1 Hz にし、リセットとスタンバイをまたいで時刻が残ります（電源が切れると 1970 から）。`settimeofday()`（SNTP の `configTime()` も）が RTC を合わせます。BKP の DR1〜DR4 はコアが使います。
- **自動では眠りません。** 呼んだときだけ。眠る前に `Serial` は送り終えます。
- **`millis()` は休止の間も進んだことになります**（AWU で測った分を足す）。AWU の系列で端子で途中で起きたときは測れないので、足さずに `timeDomain()` を進めます（RTC の系列は RTC で測れる）。休止の長さの正確さ（2026-10-28、ベンチの1個ずつ）: V003 は PC の時計と0.1%以内、X035 は3%長く、V006 は10%短く眠った（起きている間に測った AWU の速さと、眠っている間の速さが違う）。
- **WDT は休止の間は止まって見えます。** IWDG は止められないので、休止の直前に最長に延ばし、その半分ごとにコアが起きて餌をやります（ほかの処理は動かない）。起きたら `wdtEnable()` の設定に戻ります。
- **`deepSleep()`** は、RTC の系列でタイマーだけで起こし WDT が無効なら本物のスタンバイ（RAM は消え、リセットから戻る）。それ以外は `lightSleep()` と同じ休止のあとで再起動します。起床の理由は再起動をまたいで `wakeupCause()` で読めます。
- 使わなければ0 B。タイマーの休止で約1.4 KB、端子での起床を足すと約0.65 KB増えます（V003）。

設計と実測は [A の機能の全体設計](../../docs/peripheral-a-design.ja.md) の 4.3 と 5.1 にあります。

オブジェクト、examples、逃げ道が同居している理由は、Arduinoのプラットフォームが**ライブラリ経由でしかexamplesを
配れない**ことと、ライブラリには最低1つヘッダが要ることです。
他のコアがこの用途に空のダミーを置いているのに対し、
こちらはそのファイルに仕事を持たせました。

## examples

配線・操作・API と改造時の注意は [基本サンプルの使い方](examples/README.ja.md) と各フォルダの README を参照してください。

`digitalWrite()`・`analogRead()`・`tone()`などはcoreのもので、
どのライブラリにも属さないので、examplesはここに置いています。すべて共通で:

- pinはpad名(`PC0`・`PA1`)かvariantの名前(`A0`・`SDA`・`SCK`…)で書きます。
  数値は書きません。exampleが選んだpadは**exampleの都合であって変更前提**です
  — Generic boardはsiliconのseriesであって基板ではないので`LED_BUILTIN`を
  定義せず、coreが勝手に決めることもしません(`docs/board-layer-rules.ja.md`)
- 冒頭コメントに**何を示すか**と**必要な配線**を書いています
- CI で、いちばん広い板といちばん狭い板の両方でビルドしています

| example | 何を示すか |
|---|---|
| [Blink](examples/Blink/README.ja.md) | `pinMode`・`digitalWrite`・`delay` |
| [SerialEcho](examples/SerialEcho/README.ja.md) | `Serial`の入出力 |
| [AnalogRead](examples/AnalogRead/README.ja.md) | `analogRead` |
| [Fade](examples/Fade/README.ja.md) | `analogWrite` |
| [ToneMelody](examples/ToneMelody/README.ja.md) | `tone`・`noTone` |
| [PinInterrupt](examples/PinInterrupt/README.ja.md) | `attachInterrupt`とISRとの状態共有 |
| [ShiftOut](examples/ShiftOut/README.ja.md) | 74HC595への`shiftOut` |
| [PulseIn](examples/PulseIn/README.ja.md) | `pulseIn`・`delayMicroseconds` |
| [Timing](examples/Timing/README.ja.md) | `millis`・`micros`と、その巻き戻り |
| [RandomNumbers](examples/RandomNumbers/README.ja.md) | `random`・`randomSeed` |
| [AnalogResolution](examples/AnalogResolution/README.ja.md) | `analogReadResolution`・`analogWriteResolution` |
| [CriticalSection](examples/CriticalSection/README.ja.md) | `interrupts`・`noInterrupts`・`volatile` |
| [PrintFormatting](examples/PrintFormatting/README.ja.md) | `Print`・`String`・`dtostrf` |
| [TimerResource](examples/TimerResource/README.ja.md) | タイマーの取得・設定・所有権を扱う上級例 |
| [PinCapabilities](examples/PinCapabilities/README.ja.md) | このチップに実在するpad |

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
| [BoardWeActCH32X035](examples/BoardWeActCH32X035/README.ja.md) | WeAct CH32X035 Core Board（CH32X035F8U6） | USB（`SerialUSB` + 書き込み用の DFU ランタイム）、USB PD（5 V）、UART 2系統、SPI、ADC 2本、PWM、LED（PB12）。I2C は USB か SWD の端子にしか出ていないのでコメントで外してある |
| [BoardUIAPduinoProMicro](examples/BoardUIAPduinoProMicro/README.ja.md) | UIAPduino Pro Micro CH32V003 V1.4 | UART、I2C（スキャン）、SPI、ADC 3本、PWM、LED（PC0）。SPI はボードメニューの AUTO のまま。メーカーの配置図にある NSS（PC1 / PC0）は I2C と LED に重なるので、CS は PC3 にしてコメントで示す |

どちらも実機で動作を確かめています（2026-10-14）。

## 逃げ道

```cpp
#include <internal/ch32rv_registers.h>

CH32RV_TIM_ATRLR(CH32RV_TIM2_BASE) = 999;   // timerを直接叩く
```

レジスタマップ(`internal/ch32rv_registers.h`)・GPIOヘルパ(`internal/ch32rv_gpio.h`)・pinエンコード(`internal/ch32rv_pins.h`)・
route表(`internal/ch32rv_route.h`)は、名前を指して読み込みます。`CH32RV.h`は読み込みません。置き場所の`internal/`が、内部のものであることを示します。
注意書きが2つあります。

1. **この層は、版をまたいで保たれません。** 名前はベンダSDKのものではなく
   このコア独自のもので、版が変わると変わることがあります。
   版をまたいで保つものは[公開するAPI](../../docs/public-api.ja.md)にあります。
2. **coreも同じレジスタを使っています。** `Serial`や`Wire`が開いている状態で
   `AFIO_PCFR1`を直接書くと`begin()`や`setRoute()`と衝突します。
   症状は「次に開いたときにpinが動く」です。
