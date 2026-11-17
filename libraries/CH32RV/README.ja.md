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
CH32RV.wwdgEnable(20000, 10000);         // 窓ウォッチドッグ: 前の餌から10〜20 msの間に餌
CH32RV.wwdgFeed();
CH32RV.setBackupRegister(0, 42);         // リセットをまたいで残る16ビットの語（RTCの系列）
CH32RV.getBackupRegister(0);
CH32RV.setAlarm(time(nullptr) + 60, onAlarm);  // RTCのアラーム。time()の秒かstruct tmで
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
CH32RV.crc32(buf, len);                  // CRC-32/MPEG-2。CRC のハードウェアがあればそれで
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
- **窓ウォッチドッグ（WWDG）** は全系列にあります。`wwdgEnable(timeoutUs, windowUs, earlyWarning)` で、前の
  `wwdgFeed()` から `timeoutUs` のうちに餌が来なければリセット、窓を付けると `windowUs` より前の餌でもリセットします。
  PCLK1 / 4096 / 1〜8 を 63 カウントまで数えるので、最長は短く（`wwdgMaxTimeoutUs()`。48 MHz で約 43 ms、8 MHz で
  258 ms）、それを越えると `wwdgEnable()` は何も始めずに false を返します。両端とも 1 カウントの精度で、リセットが
  タイムアウトより遅れることはなく、`windowUs` 以降の餌は必ず通ります。`earlyWarning` はリセットの 1 カウント前に
  割込みの中で呼ばれます（そこで餌をやるか、理由を残す）。IWDG と同じく止められません。リセットの理由は
  `CH32RV_RESET_WINDOW_WATCHDOG`（IWDG は `CH32RV_RESET_WATCHDOG`）で区別できます。休止は餌をやりません。ストップと
  スタンバイではクロックが止まって待ちますが、休止の中の普通の WFI（RTC の系列で 1 秒未満、M030 で 150 ms 未満）は数えます
- **バックアップの語**は、リセットとスタンバイをまたいで残る 16 ビットの語です。`backupRegisterCount()`、
  `getBackupRegister(i)`、`setBackupRegister(i, v)`（STM32duino の名前、0 から）。BKP の DR5 以降で（DR1〜DR4 は
  RTC の設定）、V103・V205・L103/M103 は 6 語、V20x・V30x・V4x7 は 6 か 38 語（DR42 があるかを石に聞く）。VBAT の電池が
  あれば電源を切っても残ります。H41x と X305/X315 には BKP のデータレジスタが無く、起動で消さない RAM の 16 語
  （チェック語つき）で代わります。リセットでは残り、電源を入れ直すと 0 に戻ります（`backupRegistersKeepPower()` が
  false）。V003・V00x・M030・X033/X035 には無く、数は 0 です
- **RTC のアラーム**（RTC の系列）。暦は arduino-esp32 と同じく POSIX（`time()`・`localtime_r()`・`mktime()`・
  `settimeofday()`）です。`setAlarm(when, callback)` は `time()` の秒か、地方時の `struct tm` を取ります。コールバックは
  アラームの割込みの中で 1 回、`when` のあと 1 秒以内に呼ばれます（RTC は秒で数える）。アラームは 1 つで、呼び直すと
  置き換わり、`clearAlarm()` で消え、`getAlarm()` はまだ来ていない時刻（済めば 0）を返します。`settimeofday()` を
  またいでも同じ時刻のままです。RTC の無い系列では false。この形にした理由: arduino-esp32 には RTC のアラームの API が
  無く（暦は POSIX の時刻）、RTCZero の項目ごとの設定と一致のマスクは暦を持つ RTC 向けで、ここの RTC（32 ビットの秒の
  カウンタと比較レジスタ 1 つ）には合いません。`time()` と同じ単位の絶対時刻は、そのレジスタそのままです。繰り返しは
  コールバックの中で `setAlarm()` を 1 回呼びます
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
CH32RV.enableAlarmWakeup();                // または setAlarm() の時刻に（RTC の系列）
CH32RV.enableExtiLineWakeup(CH32RV_EXTI_PVD); // またはピンでない EXTI の線で（下）
CH32RV.wakeupCause();                      // CH32RV_WAKEUP_TIMER / _PIN / _ALARM / _EXTI_LINE / _UNDEFINED
CH32RV.timeDomain();                       // 測れない時間を飛ばすたびに増える
```

- **V103 以外の全系列で眠ります**（V103 の休止は未対応のまま（`CH32RV_SLEEP_UNSUPPORTED`。2026-11-17: アラームの直しのあとも最初の lightSleep(200 ms) のストップから戻らず、プローブも NRST も効かなかった。アラーム・BKP・WWDG は V103 でも動く。TODO））。 V003・V00x・M030・X033/X035 は AWU で起こし、V20x・V205・V30x・V4x7・L103/M103・X305/X315・H41x は RTC のアラームで起こします（ストップで眠る。H41x のストップは両方のコアが入ったときだけ効くので、V5F がスケッチを動かす間 V3F はストップで待つ。V3F のビルドでは V5F を起こさない。H41x と X305/X315 のレギュレータは通常のまま。H41x の端子での起床はまだ実機で確かめていない）。M030 の AWU は LSI / 51000 で 1 カウント約 150 ms（分周の選択が無い）なので、それ未満の端数は普通の WFI で待ち、起動後最初の休止は LSI を測る分だけ最大 300 ms 長くなります。V103・V4x7・X305/X315・M030 はビルドのみです（2026-11-15）。V103 は以前、最初の休止から起きなかったので外していました。RTC が動き始めた最初の 1 秒に眠るとアラームが設定されない不具合（V203・V307・L103・V205 で同じ止まり方）を直す前のことで、それが原因と見ています。実機ではまずここを確かめます。`CH32RV_SLEEP_UNSUPPORTED` は、休止の無いまま後から足した系列のために残しています。
- **RTC の系列では `time()` が RTC になります。** LSI を測って 1 Hz にし、リセットとスタンバイをまたいで時刻が残ります（電源が切れると 1970 から）。`settimeofday()`（SNTP の `configTime()` も）が RTC を合わせます。BKP の DR1〜DR4 はコアが使います（H41x・X305/X315 には BKP のデータレジスタが無いので RAM に置く。リセットでは残るが、VBAT の電池で電源を切ったときは RTC も始めからになる。X305/X315 にはバックアップ領域そのものが無く、RTC のクロックの設定は RTC の中にある）。
- **自動では眠りません。** 呼んだときだけ。眠る前に `Serial` は送り終えます。
- **`millis()` は休止の間も進んだことになります**（AWU で測った分を足す）。AWU の系列で端子で途中で起きたときは測れないので、足さずに `timeDomain()` を進めます（RTC の系列は RTC で測れる）。休止の長さの正確さ（2026-10-28、ベンチの1個ずつ）: V003 は PC の時計と0.1%以内、X035 は3%長く、V006 は10%短く眠った（起きている間に測った AWU の速さと、眠っている間の速さが違う）。
- **WDT は休止の間は止まって見えます。** IWDG は止められないので、休止の直前に最長に延ばし、その半分ごとにコアが起きて餌をやります（ほかの処理は動かない）。起きたら `wdtEnable()` の設定に戻ります。
- **`deepSleep()`** は、RTC の系列（スタンバイの無い H41x・X305/X315 を除く）でタイマーかアラームだけで起こし WDT が無効なら本物のスタンバイ（RAM は消え、リセットから戻る）。アラームが先に来たときは `wakeupCause()` が `CH32RV_WAKEUP_ALARM` を返し（バックアップ領域で渡す）、コールバックは呼ばれません（RAM が消えたため）。それ以外は `lightSleep()` と同じ休止のあとで再起動します。起床の理由は再起動をまたいで `wakeupCause()` で読めます。
- 使わなければ0 B。タイマーの休止で約1.4 KB、端子での起床を足すと約0.65 KB増えます（V003）。

設計と実測は [A の機能の全体設計](../../docs/peripheral-a-design.ja.md) の 4.3 と 5.1 にあります。

### ピンでない EXTI の線と PVD

PVD、RTC のアラーム、USB・ETH・LPTIM・コンパレータの起床など、`attachInterrupt()` が届かない EXTI の線です。
arduino-esp32 に同じものが無いので、形はこのコアのもの（`setAlarm()` と同じ）にしています。

```cpp
CH32RV.attachExtiLine(CH32RV_EXTI_USBFS_WAKEUP, onWake);   // RISING（か FALLING / CHANGE）
CH32RV.triggerExtiLine(CH32RV_EXTI_USBFS_WAKEUP);          // 同じ割込みをソフトウェアから（SWIEVR）
CH32RV.enableExtiLineWakeup(CH32RV_EXTI_USBFS_WAKEUP);     // 休止もこの線で終わる（CH32RV_WAKEUP_EXTI_LINE）
CH32RV.detachExtiLine(CH32RV_EXTI_USBFS_WAKEUP);
CH32RV.setLowVoltageDetect(3000, onLow);                   // VDD が 3000 mV 以上の最も低い閾値を下回った
CH32RV.isLowVoltage();                                     // 今その下にある
```

- **名前。** `CH32RV_EXTI_<名前>` は線の番号で、系列にその線とベクタがあるときだけ定義されます。
  `#ifdef CH32RV_EXTI_PVD` で分かり、無い線はコンパイルできません: `PVD`、`RTC_ALARM`、`USBD_WAKEUP`（V20x）、
  `USBFS_WAKEUP`、`USBHS_WAKEUP`、`USBHS2_WAKEUP`（V4x7）、`USBPD_WAKEUP`、`USBPD1_WAKEUP`（M030）、`USBSS_WAKEUP`、
  `ETH_WAKEUP`、`OSC32K_WAKEUP`（V20x の D8 / D8W）、`LPTIM_WAKEUP`、`LPTIM2_WAKEUP`（H41x）、`COMP_WAKEUP`、
  `LVDS_WAKEUP`（V205）、`I3C_WAKEUP`、`SWPMI_WAKEUP`、`USART_WAKEUP`。系列ごとの表は `internal/ch32rv_exti_line.h`
  で、RM と EVT から書いています（ch32-device-data にはまだ EXTI の線の表が無い）。AWU の線は休止のものなので出しません。
- **コールバック**は線の割込みの中で、エッジごとに呼ばれます。EXTI にレベルは無いので `LOW` / `HIGH` は断ります。
  フラグは呼ぶ前に消すので、その間のエッジも失いません。
- **RTC のアラームの線**は `setAlarm()` と休止が共有します。コールバックには `setAlarm()` のアラーム（とほかのアラーム）
  が届き、休止が自分のために設定するアラームは届きません。それらの要る立ち上がりは残し、`detachExtiLine()` しても
  それらが使う間は線を止めません。
- **`enableExtiLineWakeup()`** は線のコールバックをそのまま（無ければ無しで）、与えたエッジで付けます。線で起こす
  `deepSleep()` は、端子と同じく休止のあとの再起動です。
- **PVD**（`setLowVoltageDetect(mv, cb)`）: `mv` 以上で最も低い閾値（`mv` がすべてより上なら最も高いもの）を選び、
  その mV を返します。VDD がそれを下回るたびにコールバックが呼ばれ、すでに下にあればすぐに 1 回呼ばれます。
  段はデータシートの下がるときの代表値（`lowVoltageLevel(i)`。ch32-device-data から生成）: V003 2.7〜4.2 V、
  V103 2.5〜4.18 V、V20x / V30x 2.31〜3.21 V、V205 1.68〜2.97 V、V00x 1.85〜2.59 V、V4x7 2.81〜3.1 V、
  L103 1.7〜2.46 V、X035 2.1〜4.0 V、X305 / X315 2.77〜3.07 V。ESP32 のブラウンアウトのリセットではなく知らせで、
  チップは動き続けます。H41x（PVD の割込みが無い）、M030（PVD が無い）、V004 / V007 / M007（データに段が無い）では 0。
- 使わなければ 0 B。ビルドのみ（2026-11-17）。自己検査は `tests/instrumented/selfexti`。

### 道具: CRC・乱数・ミリボルト・RGB LED・割り込みの優先度

```cpp
CH32RV.crc32(buf, len);                  // CRC-32/MPEG-2。ch32rv_crc.h の ch32rv_crc32() と同じ
ch32rv_random();                         // arduino-esp32 の esp_random(): 32 ビットの乱数
ch32rv_fill_random(buf, len);            // esp_fill_random()
analogReadMilliVolts(A1);                // arduino-esp32 と同じ: 端子の電圧（mV）
rgbLedWrite(PA1, r, g, b);               // arduino-esp32 と同じ: どの端子でも WS2812 / SK6812 の LED 1 個
ch32rv_rgb_led_write(PA1, grb, n * 3);   // それを並べたチェーンを 1 つのフレームで
ch32rv_irq_set_priority(irqn, CH32RV_IRQ_PRIORITY(1));   // ch32rv_priority.h
```

- **CRC。** `CH32RV.crc32(data, len[, crc])` は CRC のハードウェアの CRC-32 です。多項式 0x04C11DB7、初期値
  0xFFFFFFFF、上位のビットから、最後の XOR なし。カタログの CRC-32/MPEG-2 で、"123456789" は `0x0376E6E7`、EVT の
  CRC_Calculation の 32 語は `0x199AC3CA` です。zlib や Ethernet の CRC-32（`0xCBF43926`）とは違います。前の結果を
  `crc` に渡すと続きを計算します。長さと番地の揃いは自由です。V103・V20x・V205・V30x・V4x7・L103 / M103・
  X305 / X315・H41x（`CH32RV_HAS_CRC_HARDWARE`）では、語の部分をハードウェアで 64 語ずつ、割り込みを止めて計算する
  ので、割り込みの中から使っても混ざりません。端数とほかの系列は 16 項目の表で、同じ値になります。H41x では一度に
  1 つのコアだけが使います。使うと V003 で約 140 バイト。
- **乱数。** `ch32rv_random()` / `ch32rv_fill_random(buf, len)` は arduino-esp32 の `esp_random()` /
  `esp_fill_random()` を、このコアの名前にしたものです（`esp_` の名前は置かない）。V30x・V4x7・H41x
  （`CH32RV_RNG_BASE`）ではハードウェアの乱数で、生の 4 語を混ぜて 1 語にします（V307 と H417 では生の語が
  繰り返し、決まったバイトの形に偏った。`ch32rv_rng_read()` は生のままで、Mbed TLS が自分で整える）。ほかの系列と、乱数の回路が直らない誤りを返したときは、石の固有の
  ID と呼んだときの SysTick の値を混ぜたソフトの値です。石ごと・実行ごとに変わりますが予測でき、**暗号には使えません**
  （誤りを false で返すのは `ch32rv_rng_read()`）。`random()` / `randomSeed()` は Arduino の決まりのままです:
  `randomSeed()` を呼ばなければ毎回同じ列、`randomSeed(n)` で同じ列の再現。Arduino のリファレンスが両方を書いて
  おり、変えると乱数の回路の有無で系列ごとに動きが変わるためです。arduino-esp32 は `randomSeed()` を呼ぶまで
  `random()` をハードウェアの乱数にしますが、ここでは `randomSeed(ch32rv_random())` で、どの系列でも実行ごとに
  違う列になります。
- **`analogReadMilliVolts(pin)`**（arduino-esp32 と同じ名前）。**電源電圧は呼ぶたびに測ります。** 端子を変換する
  直前に ADC の内部の基準電圧（Vrefint）を変換し、mV = Vrefint の代表値（mV） x 端子の読み取り / Vrefint の読み取り
  で求めます。Vrefint から VDD を求めて読み取りを換算するのと同じで、VDD を仮定しません。例外はデータに Vrefint の
  無い M030 だけで、そこでは VDD を 3300 mV とします。読み取りには `analogReadCalibrated()` と同じ系列の補正が
  かかります。
  - **1 回の時間: 変換 4 回**（Vrefint を 2 回と端子を 2 回。チャネルを切り替えた最初の 1 回は捨てる）。
    `analogRead()` は 1 回です。どれも一番長いサンプル時間で約 250 ADC クロックなので、ADC のクロックが 12 MHz
    （V20x / V30x の 96 MHz）なら 1 回約 85 µs、一般に `analogRead()` の 4 回分です。CH32V103 はさらに 20 µs
    かかります（基準電圧を毎回入れて、終わったら切る。入れたままだと、そこでは端子が正しく読めない）。V003 で
    `analogRead()` より約 300 バイト多くなります。
  - **精度はまだ測っていません。** 誤差は Vrefint の代表値からのばらつき（データシートの min / max、数 %）と、
    変換器のオフセットと利得の分です。自動の試験は、駆動した端子が 1 で電源の 80〜110 %、0 で 10 % 以下かを
    見るだけです。特性は外部の電圧源を使うマニュアルの試験で取ります。
- **`rgbLedWrite(pin, r, g, b)`** は WS2812 / SK6812 型の LED 1 個分の 24 ビット（GRB）をどの端子にも送って戻ります。
  ほかの色の順は `rgbLedWriteOrdered(pin, LED_COLOR_ORDER_RGB, ...)`、`neopixelWrite()` は非推奨の古い名前で、
  3 つとも arduino-esp32 の名前です。**チェーンは `ch32rv_rgb_led_write(pin, bytes, len)`** で、全部の LED のバイトを
  送る順に（WS2812B は GRB、RGBW の部品は 4 バイト）1 つのフレームで送り、その間ずっと割り込みを止めます（LED 1 個
  につき約 30 µs）。このコア独自の関数です（arduino-esp32 にチェーンの呼び出しは無い。形は Adafruit_NeoPixel の
  `show()`）。`rgbLedWrite()` を続けて呼ぶと、LED の間で線が呼び出しの間 Low になるためです。多くの板で 2〜5 µs、
  V006 の 24 MHz では 13 µs で、そこで LED がラッチしてチェーンが切れました（ベンチ、2026-11-17）。目標は T0H
  375 ns、T1H 800 ns、1 ビット 1.25 µs で、72 MHz 以上はどの縁も SysTick で、それ未満は RAM に置いた命令の数で作ります
  （8 MHz ではループそのものが長く 1 ビット 1.625 µs。36 MHz 未満では、次のバイトを読む各バイトの最後のビットが最大
  4 サイクル長い）。どちらもラッチは待ちません。そのあと線を LED のリセットの時間（WS2812 で 50 µs、WS2812B-V5 など
  新しい部品で 280 µs）Low に保ちます。実測（2026-11-17、WireSkein の rgb_led、60 MS/s、LED 1 個）: V003・V006・V103・
  V203・V205・X035・H417 で T0H 333〜433 ns、T1H 767〜900 ns、1 ビット 1.22〜1.43 µs。
  **V4x7 と X3x5 では、ARGB の端子（V4x7 は PA15、X3x5 は PA15・PD3・PD4）で WS2812 のブロック（ARGB）が送ります。**
  DMA で送り、割り込みは止めません（ESP32 の `rgbLedWrite()` が RMT を使うのと同じ）。関数も目標の時間（HCLK の数で）も
  同じで、チェーンは 65535 バイトまで（それより長いものはソフトで送る）。戻るのは最後のビットが出てからです（戻れば
  バッファーを書き換えてよく、フレームは呼んだ順に出る）。非同期の呼び出しはありません。どちらで送るかは
  `ch32rv_rgb_led_is_hardware(pin)` で分かります。2026-11-17、ビルドのみ。
- **割り込みの優先度**（`#include <ch32rv_priority.h>`）: `ch32rv_irq_set_priority(irqn, priority)` /
  `ch32rv_irq_get_priority(irqn)` は PFIC の優先度のバイトで、WCH の `NVIC_SetPriority()` と同じく小さいほど
  急ぎです。`CH32RV_IRQ_PRIORITY(level)` は割り込みのレベル（0 から `CH32RV_IRQ_PREEMPT_LEVELS - 1`）のバイトです。
  V003・V00x・V103・V20x・V205・V4x7・X035・L103 / M103・X305 / X315・H41x の V3F は 2 レベル、V30x と M030 は 4、
  H41x の V5F は 8（スタートアップの INTSYSCR と EVT の core_riscv.h による）。急ぎのレベルの割り込みは、それより
  遅いレベルの処理に割り込みます。深さはハードウェアのレジスタのスタックまで（V30x は 4 レベルで 3 段）。割り込みの
  番号は `CH32RV_IRQN_<名前>`（EVT の名前）、タイマーの capability の `update_irq`、`ch32rv_pin_irqn(pin)`（端子の
  EXTI のベクタ。グループで共有）、`CH32RV_IRQN_CORE_SYSTICK` です。**コアの割り込みとの約束:** コアは優先度を
  書きません。SysTick（millis()）・USART・tone() / Servo のタイマー・EXTI・USB・RTC のアラーム・WWDG の早期警告の
  割り込みはどれも 0（一番急ぎ）のままで、スケッチが設定するまで入れ子は起きません。下げた割り込み処理にはコアの
  割り込みが割り込みます（その間も millis() と Serial が動く）。コアはこの使い方を前提に作っています。コアの割り込み
  を下げてもかまいません。コアのスレッド側のコードは、割り込み処理と共有するものを全割り込みのマスクで守るので、
  どのレベルでも成り立ちます。ただしドライバーは再入できないので、あるドライバーの割り込み処理に割り込める処理から
  そのドライバーを呼びません。`noInterrupts()` はすべてのレベルを止めます。H41x はコアごとに別です。HPE の無い
  V103 は EVT のとおり PFIC で入れ子にしますが、実機ではまだ確かめていません。

### H41x の 2 つのコア（V5F と V3F）

CPU Core = V5F（既定）では、スケッチは V5F で動き、V3F を 2 つ目のコアとして使えます（`setup1()` / `loop1()`、
`CH32RV.startV3F()`、例 [DualCoreV3F](examples/DualCoreV3F/README.ja.md)）。2 つのコアの間の排他と通知は次の API です
（C の関数は `ch32rv_multicore.h`）。どちらのコアからも同じ呼び方です。

```cpp
CH32RV.hsemTake(0);                 // ハードウェアのセマフォ（HSEM）のチャネル 0 を取る（待つ）
shared.a = 1; shared.b = 2;          // 2 つのコアで共有するもの
CH32RV.hsemRelease(0);              // 離す
if (CH32RV.hsemTryTake(1)) { ... }  // 待たずに試す

CH32RV.ipcOnReceive(onMessage);     // このコアに届いたメッセージで onMessage(uint32_t) が割り込みで動く
CH32RV.ipcSend(42);                 // 相手のコアへ（前のメッセージが取られるまで待つ）
uint32_t m; CH32RV.ipcReceive(m);   // コールバック無しで取る
```

- **HSEM**（`hsemCount()` = 31。チャネル 31 はコアが使う）: 取ったコアだけが離せます。相手のコアのチャネルを
  `hsemRelease()` しても false で、取られたままです。チャネルはコアのもので、同じコアの割り込み処理は、そのコアの
  ループが持つチャネルを取れません（`hsemTryTake()` が false。割り込みの中で待たないこと）。`hsemTake(ch, ms)` は
  `ms` で諦め（既定は無期限）、このコアが既に持つチャネルにはすぐ false です。
- **IPC**: 向きごとに 32 ビットのメッセージ 1 つ（IPC のチャネル 0 が V5F へ、1 が V3F へ。2 と 3 はスケッチに
  残しています）。`ipcSend(msg, ms)` は前のメッセージが取られるまで `ms` 待ち（既定は無期限、割り込みの中では 0）、
  入れば true。コールバックはそれを登録したコアの IPC の割り込み（V5F は IPC_CH0、V3F は IPC_CH1）で動きます。
  相手がまだ動いていなくても、メッセージは届くまで待ちます。CPU Core = V3F では V5F が眠ったままなので、IPC の 3 つは
  false です（HSEM はそのまま使えます）。
- **V3F で使えるもの:** V3F に渡した関数は、割り込みを有効にして動きます。V3F で有効にした割り込みは V3F で
  受けます（下位 32 本はコアごと、それより上は有効にしたコアへ移る。RM 4.7.5.45）。V3F で `begin()` した
  `Serial1` などの周辺、`pinMode()` / `digitalWrite()`、`attachInterrupt()`、タイマーのコールバック、`millis()` /
  `delay()` が使えます。優先度は V3F の 2 レベル（bit 7）です。
- **約束:** 1 つの周辺（USART・タイマー・SPI・I2C・ADC など）は 1 つのコアだけが使います。V5F の `Serial`
  （と `printf`、コアのコンソール）は V5F のもので、V3F は別のもの（`Serial1` など）を使います。1 本の端子、
  1 つの EXTI のベクタのグループ（EXTI9_5、EXTI15_10）も 1 つのコアのものです。複数のドライバーが共有するレジスタ
  （RCC のクロック許可、ポートの設定、AFIO、EXTI）は、V3F を起こしたあと、コアがロック（HSEM のチャネル 31）の中で書くので（V3F を使わないスケッチには入らない）、同じ
  ポートの別の端子を 2 つのコアで分けてかまいません。
- **V3F で使わないもの:** システムのサービス（USB、IP のスタック、USB PD）、休止・`restart()`・ウォッチドッグ・
  RTC・フラッシュ（`CH32RV.*`）、命令の数で時間を作るもの（`rgbLedWrite()`：`F_CPU` は V5F の 400 MHz）、ヒープ
  （`malloc`・`new`・`String`。picolibc の malloc にロックが無い）。
- 例: [DualCoreMessages](examples/DualCoreMessages/README.ja.md)。試験は selfapi の `test_cores_hsem`・
  `test_cores_ipc`・`test_cores_v3f_serial`（1 つのイメージに両方のコアのコードが入るので、書き込みは 1 回）。

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
| [AnalogRead](examples/AnalogRead/README.ja.md) | `analogRead`・`analogReadMilliVolts` |
| [AnalogContinuous](examples/AnalogContinuous/README.ja.md) | `analogContinuous`・`analogContinuousRead`（arduino-esp32 の連続 ADC の API） |
| [Fade](examples/Fade/README.ja.md) | `analogWrite` |
| [ToneMelody](examples/ToneMelody/README.ja.md) | `tone`・`noTone` |
| [PinInterrupt](examples/PinInterrupt/README.ja.md) | `attachInterrupt`とISRとの状態共有 |
| [LowVoltageDetect](examples/LowVoltageDetect/README.ja.md) | `CH32RV.setLowVoltageDetect`、`triggerExtiLine`（PVDのEXTIの線） |
| [ShiftOut](examples/ShiftOut/README.ja.md) | 74HC595への`shiftOut` |
| [PulseIn](examples/PulseIn/README.ja.md) | `pulseIn`・`delayMicroseconds` |
| [TimerAlarm](examples/TimerAlarm/README.ja.md) | `timerBegin`・`timerAttachInterrupt`・`timerAlarm`（arduino-esp32 のタイマー API） |
| [Timing](examples/Timing/README.ja.md) | `millis`・`micros`と、その巻き戻り |
| [RandomNumbers](examples/RandomNumbers/README.ja.md) | `random`・`randomSeed`・`ch32rv_random`・`ch32rv_fill_random` |
| [Crc32](examples/Crc32/README.ja.md) | `CH32RV.crc32`（1 回で、分けて） |
| [RgbLed](examples/RgbLed/README.ja.md) | 1 個は `rgbLedWrite`、チェーンは `ch32rv_rgb_led_write` |
| [InterruptPriority](examples/InterruptPriority/README.ja.md) | `ch32rv_irq_set_priority`、コアの SysTick の下での入れ子 |
| [AnalogResolution](examples/AnalogResolution/README.ja.md) | `analogReadResolution`・`analogWriteResolution` |
| [CriticalSection](examples/CriticalSection/README.ja.md) | `interrupts`・`noInterrupts`・`volatile` |
| [PrintFormatting](examples/PrintFormatting/README.ja.md) | `Print`・`String`・`dtostrf` |
| [TimerResource](examples/TimerResource/README.ja.md) | タイマーの取得・設定・所有権を扱う上級例 |
| [PinCapabilities](examples/PinCapabilities/README.ja.md) | このチップに実在するpad |
| [DualCoreV3F](examples/DualCoreV3F/README.ja.md) | CH32H417: 2 つ目のコアでの `setup1()` / `loop1()` と `CH32RV.startV3F()` |
| [DualCoreMessages](examples/DualCoreMessages/README.ja.md) | CH32H417: 2 つのコアの間の `CH32RV.ipcSend()` と `CH32RV.hsemTake()`、V3F の `Serial1` |

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
