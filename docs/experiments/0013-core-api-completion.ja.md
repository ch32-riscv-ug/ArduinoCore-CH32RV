# 実験0013: コアAPIの一通り実装と実機自己検証

実施日: 2026-08-20
対象: Milestone 1の周辺、Q-013(内部HAL contract)
実施環境: **CH32V203実機 + WCH-LinkE v2.12**、probe-rs 0.32.0経由の書き込み

## 目的

ArduinoCore-APIが宣言していて実体が無い関数は、**sketchを書いた瞬間にlink errorになる**。
どれが足りないかを実測し、埋める。

```
$ arduino-cli compile <全API利用sketch> | grep "undefined reference"
analogRead / analogWrite / attachInterrupt / detachInterrupt / noTone /
pulseIn / random / randomSeed / shiftIn / shiftOut / tone      -- 11件
```

加えて`digitalPinToInterrupt`が未定義でcompileも通らなかった。

## 実装

| file | 内容 |
|---|---|
| `wiring_analog.c` | `analogRead`(ADC1)、`analogReference`、`analogReadResolution` |
| `wiring_pwm.c` | `analogWrite`(TIM1/2/3)、`analogWriteResolution` |
| `wiring_interrupts.c` | `attachInterrupt`/`attachInterruptParam`/`detachInterrupt`(EXTI) |
| `wiring_shift.c` | `shiftIn`/`shiftOut`/`pulseIn`/`pulseInLong` |
| `wiring_math.cpp` | `random`/`randomSeed`(xorshift32)、`tone`/`noTone`はstub |

**`wiring_math.cpp`だけC++**なのは、`api/Common.h`が`random`等を`extern "C"`ブロックの
**外**で宣言しているため。`pulseIn`は内側なのでCでよい。この境界はheaderを読むまで分からない。

`random(long)`にnewlibの`random()`を使うと**同一名前空間で衝突する**ので、
xorshift32を自前で持った。libc非依存で数命令。

### 生成物が増えた

family差は全て生成側へ寄せた。`build.core_defines`(生成物)で渡す値:

| define | 用途 |
|---|---|
| `CH32_ADC_BITS` | ADC分解能。V003のみ10bit、他は12bit |
| `CH32_FLASH_LATENCY` | flash wait state。X035は48MHzで**2が必須** |
| `CH32_EXTIS` | EXTI vectorのグループ表(下記) |

**EXTIのvector分割はfamilyで2方式ある**:

| 方式 | family | vector |
|---|---|---|
| A | V003 / V00x / M030 / X035 / X3x5 | `EXTI7_0`、`EXTI15_8` |
| B | V20x / V30x / V4x7 / L103 / V205 | `EXTI0`〜`EXTI4`、`EXTI9_5`、`EXTI15_10` |

手で書くとfamily追加のたびに直す羽目になるので、
**vector tableからhandler名と担当ビットを生成**する(`exti_<variant>.h`)。
`wiring_interrupts.c`はX-macroで展開するだけなので、方式Cが来ても変更不要。

PWMのpin→timer/channelも同様に生成した(default routeのみ、TIM1/2/3)。
timerのsignal名も**family間で正規化されていない**(`TIM1_CH1` / `T1CH1` / `T1C1`)。

## 実機で見つかったbug: ring bufferのlost update

自己検証sketchを実機で走らせたところ、**行の途中で文字が空白に化けた**:

```
millis PASS
   ros PASS      <- "micros" の先頭3文字が 0x20 になっている
digital PASS
```

原因は`api/RingBuffer.h`の`_numElems`。push側とpop側の**両方が
read-modify-writeする単一カウンタ**で、UARTは:

- TX: sketchがpush、**ISRがpop**
- RX: **ISRがpush**、sketchがpop

と、必ず2つの実行文脈から触る。lost updateでbuffer indexがずれ、
別の場所のバイトを送ってしまっていた。

**修正**: `ch32_ringbuffer.h`を自前で用意した。producerは`head`だけ、
consumerは`tail`だけを書くSPSC方式で、**lockも割込み禁止も要らない**。
代償は1スロット使えないことだけ。

修正後、同じsketchを3回連続で流して化けゼロ。

これは**実機testでしか出ない類のbug**である。compileもstatic checkも通っていた。

## 自己検証sketch

[`tests/sketches/basic/core_api/`](../../tests/sketches/basic/core_api/)。
**外部配線を一切必要としない**ように組んだ:

- **割込み**: 出力に設定したpinを自分でtoggleする。CH32は出力pinでも入力経路が
  生きているのでEXTIが立つ。jumper不要
- **digitalRead**: 同じく出力pinを読み返す
- **analogRead**: 値ではなく範囲(0..1023)だけ見る。未接続でも成立する
- **pulseIn**: timeoutが効くこと(hangしないこと)を見る
- **random**: 同じseedで同じ値が出ること

判定はtarget側で行い、`<name> PASS` / `<name> FAIL <値>`を出す。
pytest側は名前でassertするので、失敗したら**どのAPIかが即分かる**。

### 実機結果(CH32V203)

13 check全てPASS、`failures=0`。`Print`の書式(`fmt=FF,-42,1.50`)も一致。

## 未確認

- `tone()`は無音stub。timer channelの排他管理が要る
- X033/X035のEXTI線16〜23(`EXTI25_16`)。`AFIO_EXTICR`の追加wordが要る
- X305/X315のPWM。timerもper-pin AF方式で、default routeが存在しない
- ADC分解能は datasheet 由来。実機での確認はまだ
