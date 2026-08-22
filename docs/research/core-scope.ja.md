# R-25: coreに置くものと、ライブラリに出すもの

日付: 2026-08-21
状態: **`SerialSDI`の移動・stdout差し替え・examples規約は実施済み。判断基準のADR化は未**
関連: [ADR-0009](../adr/0009-arduinocore-api-import.ja.md)、[ADR-0012](../adr/0012-usb-stack.ja.md)

## 問い

`SerialSDI`はcore APIなのか。もっと言えば、**coreの範囲は何で決まるのか**。

## 判断基準(案)

coreに置くのは、次の**いずれかに当てはまるものだけ**とする。

1. **ArduinoCore-APIが宣言している**。sketchが`#include`なしに呼べるもの
   (`pinMode`、`Serial`、`tone`、`millis`…)
2. **どのsketchもリンクに必要**。起動・libc・例外まわり
   (crt0、`main()`、syscalls、`_sbrk`、C++サポート)
3. **同梱ライブラリが土台にするHAL契約**。ライブラリ側から見た「coreが提供する約束」
   (`ch32_registers.h`、`ch32_gpio.h`、`ch32_pins.h`、`ch32_route.h`、variantの`pins_arduino.h`)

3つのどれでもないものは、**`#include`して初めて使えるのだからライブラリ**。

## 現状のcoreを全部あてはめる

| ファイル | 該当 | 判定 |
|---|---|---|
| `Arduino.h` / `api/` | 1 | core |
| `main.cpp` | 2 | core |
| `crt0_ch32.S` / `sections.ld` / `vectors_*.inc` | 2 | core |
| `syscalls.c` / `ch32_sbrk.c` / `cxx_support.cpp` | 2 | core |
| `itoa.c` / `dtostrf.c` | 1(AVR互換としてsketchが直接呼ぶ) | core |
| `wiring_digital.c` / `analog` / `pwm` / `time` / `shift` / `math` / `interrupts` | 1 | core |
| `wiring_tone.cpp` | 1(`tone`/`noTone`はAPIが宣言) | core |
| `HardwareSerial.{h,cpp}` | 1(`Serial`は`#include`不要) | core |
| `ch32_ringbuffer.h` / `ch32_serial_write.h` / `ch32_clock.h` | 2(上の実装に要る) | core |
| `ch32_registers.h` / `ch32_gpio.h` / `ch32_pins.h` / `ch32_route.h` | 3 | core |
| `irqn_*.h` / `exti_*.h` | 2(生成物) | core |
| **`SerialSDI.{h,cpp}`** | **どれにも当てはまらない** | **ライブラリへ** |

`SerialSDI`は`#include <SerialSDI.h>`して初めて使えるもので、
ArduinoCore-APIのどこにも出てこない。coreに置いた理由は
「`Serial`の仲間だから」以上のものが無かった。

**coreの他のファイルは1つも`SerialSDI`を参照していない**ことも確認した。

### 移動しても利用者からは変わらない

Arduinoのライブラリは`src/`がinclude pathに入るので、
`libraries/SerialSDI/src/SerialSDI.h`に置いても綴りは`#include <SerialSDI.h>`のまま。
**sketchの書き方は1文字も変わらない。**

## printf/monitorの出力先(実装済み)

移動にあたって1つだけ設計が要った。`printf`の出力先である。

以前はcore内で結線が閉じていた。

```c
extern "C" size_t ch32_serial_write_bytes(const uint8_t *data, size_t len)
{
    return SERIAL_PORT_MONITOR.write(data, len);   /* コンパイル時に固定 */
}
```

これだと「`printf`をSDIへ」「`printf`をUSB CDCへ」を**coreがそのライブラリを知らないと実現できない**。
SDIとUSB CDCで2回同じ問題を解くことになるので、**1つの口にまとめた**。

```cpp
void ch32_set_stdout(arduino::Print *out);   /* nullptr で捨てる */
arduino::Print *ch32_get_stdout(void);
```

- 既定は**リンク時に**monitor portを指す。`ch32_set_stdout()`を呼ばないsketchの挙動は従来どおり
- `Print`派生なら何でも刺さるので、**coreはSDIもUSB CDCも知らないままでよい**
- 動かすのは**stdioだけ**。`Serial`という名前はコンパイル時に決まるので追随しない。
  ここを混ぜると「`Serial.println`とprintfで出力先が違う」という説明のつかない状態になる
- 実行時ポインタなので、開いていないportへ書いても`HardwareSerial::write`が0を返すだけ
  (`Serial.begin()`前の`printf`が無言で捨てられる従来の挙動もそのまま)

USB CDCが来たときも**この口をそのまま使う**。
`Serial`自体をCDCにするかどうかはFQBNメニューの話で、別に決める。

## 逆方向: ライブラリに出しすぎているものは無いか

`Wire` / `SPI` / `Servo` / `TinyUSB`はどれも`#include`が要るのでライブラリで正しい。
ただし**Servoはcoreと暗黙に結びついている**点は記録しておく:
使うtimerをvariantが選び(`CH32_SERVO_TIMER`)、`tone()`と衝突しないよう
**generator側で排他を決めている**。つまりtimerの取り合いはcoreの責任で、
ライブラリはその結果を使うだけ。この線引き自体は妥当だが、
将来「ユーザーがtimerを選べる」ようにするなら**core側にtimer調停の口**が要る。

## 同梱ライブラリの方針(提案)

「何を同梱するか」の基準が無いと、際限なく増える。案:

| 基準 | 例 |
|---|---|
| **Arduinoの事実上の標準API**を提供するもの | `Wire`、`SPI`、(将来)`EEPROM` |
| **coreの機能を出すのに必要**なもの | `TinyUSB`(USBスタック)、`SerialSDI`(このチップ固有の出力経路) |
| **ハードウェア調停がcore側に必要**で、外部ライブラリでは書けないもの | `Servo`(timerの排他) |

逆に、**上の3つに当てはまらないものは同梱しない**。
センサやディスプレイのドライバは外部ライブラリで足りる。

## examplesをどこに置くか(方針案)

ライブラリに切り出すとexamplesが**その機能の隣に置ける**。これは分離の副産物ではなく利点。

### 置き場所

| examplesの対象 | 置き場所 |
|---|---|
| ライブラリの機能 | そのライブラリの`examples/` |
| **coreのAPI**(`digitalWrite`、`analogRead`、`tone`、`Serial`…) | **`libraries/CH32/examples/`** |

Arduinoのプラットフォームは**ライブラリ経由でしかexamplesを配れない**。
ESP32も同じ問題に当たっていて、`libraries/ESP32/`を作り、
IDEの警告を黙らせるためだけの`src/dummy.h`を置いている。

```text
// This file is here only to silence warnings from Arduino IDE
// Currently IDE doesn't support no-code libraries, like this collection of example sketches.
```

こちらは**そのファイルに仕事を与えた**。`libraries/CH32/src/CH32.h`は
レジスタマップとGPIOヘルパを束ねる「逃げ道」の入口で、
ダミーではないのに同じ役目(IDEの警告回避)も果たす。

### 書き方の規約

- **1 example = 1ディレクトリ**、`<Name>/<Name>.ino`(arduino-cliが要求する)
- 冒頭コメントに**何を示すか**と**配線**を書く。配線不要ならそう書く
- **pinはvariantの名前から取る**。`LED_BUILTIN` / `SDA` / `SCL` / `SCK` / `MISO` / `MOSI` / `SS` / `A0`。
  数値リテラルを書かない。そうすれば全boardで動く
- **examplesはテストではない**。PASS/FAILを出さない。自己検査は`tests/sketches/basic/`の役目
- **CH32V003(16KB flash / 2KB RAM)に載ること**。載らないなら冒頭にそう書く
- 各ライブラリに`keywords.txt`を置く(IDEの色付け。ESP32はexamples専用ライブラリにも置いている)

### CIで焼く

[tests/compile/test_examples.py](../../tests/compile/test_examples.py)が**全examplesを2 boardでコンパイル**する
(X035=主対象、V003=下限)。examplesはテストに触られない唯一のコードなので、
放っておくと真っ先に腐る。現在11 example × 2 board。

## `ch32_registers.h`がsketchから見えることの懸念

見えていること自体は問題ないと考える(ESP32もSTM32duinoもレジスタヘッダを公開している)。
ただし懸念は3つあり、うち1つは**うち固有**で重い。

1. **安定性の約束ができない(これが重い)**。ESP32やSTM32duinoが公開しているのは
   **ベンダが維持しているヘッダ**で、名前はまず変わらない。
   一方`ch32_registers.h`は**このコアの自作**で、しかも
   [R-20](register-map-data.ja.md)で「いずれ`ch32-device-data`から生成する」と言っている。
   **名前が変わる前提のものを公開している**ことになる。
   → 対処: `CH32.h`の冒頭で「この層は安定ではない」と明記した。
     使ったsketchはコアのバージョンに固定される、と読める形にしてある
2. **coreと取り合いになる**。sketchが`AFIO_PCFR1`を直接書けば、
   次の`Serial.begin()`や`setRoute()`と衝突する。症状は「再初期化したらpinが動いた」。
   → 対処: 同じく`CH32.h`に明記
3. **名前の衝突**。`CH32_`接頭辞のものは安全だが、
   今回足した`SDA`/`SCL`/`MOSI`/`MISO`/`SCK`/`SS`は接頭辞が無い。
   これはArduinoの慣習どおりで、他コアも同じ問題を抱えている(ライブラリのenum名と衝突しうる)。
   慣習を捨てる方が害が大きいので、このままとする

**もう1つ、これから増える懸念**: TinyUSB用のshim([R-23](tinyusb-vendor-header.ja.md))は
`variants/<SERIES>/ch32v20x.h`という**ベンダヘッダと同名のファイル**になる。
これもsketchから見えるので、`#include <ch32v20x.h>`が「WCHのSDKヘッダだ」と誤解されうる。
ヘッダ冒頭に明記する予定だが、`ch32_registers.h`より紛らわしいのは確か。

## 未決(判断をお願いしたい)

- [ ] この判断基準(1〜3)を採るか。採るならADRにする
- [ ] 同梱ライブラリの方針を採るか
- [x] `ch32_registers.h`等がsketchから見えることは**認める**。
      懸念と対処は上記。`libraries/CH32/src/CH32.h`が入口で、
      「安定ではない」「coreと取り合いになる」を冒頭に明記した
- [x] printf/monitorの出力先: **`ch32_set_stdout(Print*)`で実装済み**。
      USB CDCが来ても同じ口が使える
