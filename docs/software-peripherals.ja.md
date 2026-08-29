# ソフトウェア実装ペリフェラル

- 状態: **提案**(ADR未起票)
- 文書基準日: 2026-08-29
- 関連: [board-layer-rules](board-layer-rules.ja.md)(pinの層)、
  [ADR-0013](adr/0013-bundled-libraries.ja.md)(同梱ライブラリ)、
  [examples-build-rules](examples-build-rules.ja.md)(追加コスト)

## 0. 前提

リリース前で実ユーザがいない。破壊的変更をしてよい。

## 1. 動機

CH32はpadが少ない部品が多く(CH32V003 SOP8はGPIO 6本)、ハードウェアのバスは
**routeで決まった数組のpadにしか出せない**。padが足りない・既に埋まっている・
そのpackageにbondされていない、という理由でハードのバスが使えない場面がある。

ソフト実装(bit-bang)は遅いが**どのpadでも使える**ので、その逃げ先になる。

**使わなければコストは無い。** Arduinoのライブラリは`#include`されたときだけ
リンクされるので、同梱しても使わない利用者のバイナリは1バイトも増えない。
したがって「実装できる範囲は用意しておく」という方針が取れる。

ただしコストがゼロなのは**利用者のバイナリだけ**である。維持コストと、
examplesのsweep(1 exampleあたり最大24 series × 約2.2秒)は増える。

## 2. 前提資源(2026-08-29 実測)

| 事実 | 影響 |
|---|---|
| `HardwareI2C`(`Stream`派生) / `HardwareSPI` の基底クラスがある | **ドロップイン互換にできる**。`TwoWire&`/`SPIClass&`を取る既存ライブラリがそのまま動く |
| `pinMode(OUTPUT_OPENDRAIN)` がネイティブ(GPIOのCNFビット) | I2Cで「INPUT⇄OUTPUT切替でHi-Zを作る」小細工が不要。SCLを読めるのでクロックストレッチ対応も可 |
| `digitalWrite`はBSHR/BCRへの**単一ストア** | read-modify-write無し、割り込みに対して原子的。pinがリテラルならport/bitは定数畳み込み |
| `delayMicroseconds()`は`micros()`のビジーループ | µs級のビット幅は作れるが、**1µs前後の精度は出ない**(WS2812には不足) |
| SysTickは**コアが1ms compareで使用中**(`SysTick_Handler`) | 追加のtick hookは無い。ソフトペリフェラルの時間軸は「ビジーウェイト」か「空きTIM」 |
| EXTI線は**padのbit番号**で決まる(`digitalPinToInterrupt(pin) = pin`) | PA3とPB3は同じEXTI3。受信にEXTIを使う実装は**線の取り合いになる** |
| **CH32V003は空きタイマがゼロ**(TIM1/TIM2のみ、tone/Servoが使用) | タイマを要求する実装はV003で成立しない。他23 seriesは最低1本空く |

flashコスト(CH32V003、16K/2K、Serialのみのsketchを基準):

| | text増分 |
|---|---|
| Wire(ハード) | +2608 |
| SPI(ハード) | +1056 |
| `shiftOut()` | +352 |

**当初「ソフトのほうが小さくなる」と見積もったが、実測で外れた**(2026-08-29)。
SoftSPIは**+1376**で、ハードの+1056より大きい。理由は2つ:

- `digitalWrite()`はエッジごとの**関数呼び出し**で、ペリフェラルのレジスタ1回の
  書き込みより高くつく。`transfer()`単体で290 B
- `HardwareSPI`を継承すると**vtableが全overrideを保持する**ので、
  `--gc-sections`が落とせない。vtable 60 B + 未使用overrideぶん

つまり**ドロップイン互換それ自体にflashコストがある**。それでも16 Kに余裕で入り、
`#include`しない利用者には一切かからないので、方針は変わらない。
以降の見積りは「ハードと同程度かやや大きい」を前提にすること。

既にコアにあるソフト実装: `shiftOut()` / `shiftIn()` / `pulseIn()`。

## 3. 判定基準

> **ソフトで出してよいのは、(a) タイミング要件を`delayMicroseconds`か空きTIMで満たせ、
> (b) 既存の基底クラスかArduino慣習のAPIに載り、(c) 遅いことが機能を壊さないもの。**

「遅いことが機能を壊さない」が効くのはバスの向き。**クロックを自分が出す
(master)なら遅くても相手が待つ**。相手がクロックを出す・自走する(slave、
CAN、I2S、USB)ものはソフトでは成立しない。

### コアが持つ範囲(2026-08-29決定)

> **コアが出すのは「コア自身がハードウェアで持っているバスの、ソフト代替」まで。**
> **ハードに対応物が無いデバイスプロトコルは外部ライブラリの領分。**

技術的に作れることと、ここに置くべきことは別である。SoftSPI / SoftWire /
SoftSerial は`SPI`/`Wire`/`Serial`の代替で、**同じAPIの別実装**として並ぶから
コアに属する。padが足りないときの逃げ先という動機も、コアのバスの話である。

1-Wire・WS2812・PS/2はそうではない。コアに対応するハードウェアが無く、
**それ自体が1つのデバイスプロトコル**で、既に維持された外部ライブラリがある
(1-Wireなら`PaulStoffregen/OneWire`)。ここで抱えても二重実装になる。

## 4. 候補一覧

| 候補 | 判定 | 根拠 |
|---|---|---|
| **SoftSPI (master)** | **採用** | クロック主導。MISO・mode 0〜3まで素直 |
| **SoftWire (I2C master)** | **採用・最優先** | オープンドレインがネイティブ。100k/400kは元々低速。I2Cデバイスが最も多く、逃げ先としての効果が最大 |
| **SoftSerial (TX)** | **採用** | `delayMicroseconds`でビット幅を作るだけ。ブロッキング送信 |
| SoftSerial (RX) | 条件付き | EXTIでstart bit検出→ビジーループでサンプル。**EXTI線がbit共有**、ボーレート上限がF_CPU依存。TXと分けて判断する |
| OneWire (1-Wire master) | **見送り** | 技術的には容易(µs級+オープンドレイン)だが、**ハードに対応物が無いデバイスプロトコル**で、`PaulStoffregen/OneWire`という定番がある。コアの範囲外 |
| SoftPWM | 条件付き | 空きTIMが要る。**V003では成立しない**。ハードPWMが8 padしかない部品では価値がある |
| WS2812(ソフト) | 見送り | 同じ理由でデバイスプロトコル。加えて1.25µs/bitは`delayMicroseconds`では届かず、専用の調整済みループが要る |
| ARGB(ハード) | 別件 | V407/V467/X305/X315は**ハードARGBを持つ**。これは「コアが持つペリフェラル」なので本文書の対象外だが、todoに起こす価値がある |
| PS/2 host | 見送り | デバイスプロトコルで、需要も薄い |
| SWD/JTAG host | 見送り | bit-bang可能だが本コアの役割ではない |
| I2S | **不可** | 連続した高速ビットクロックが要る |
| CAN | **不可** | ビットタイミングと調停がMHz級 |
| USB | **不可** | V003にハードが無く、ソフトlow-speedは24MHz rv32ecで非現実的 |
| ADC / DAC / コンパレータ | **不可** | アナログはソフトで作れない |
| I2C slave / SPI slave | **不可** | 相手がクロックを出すため、ビジーループでは追従を保証できない |

## 5. 各仕様

共通:

- クラス名は`Soft`接頭辞(`SoftSPI` / `SoftWire` / `SoftSerial` / `OneWire`)
- **pinはコンストラクタ引数**。既定値を持たない。
  [board-layer-rules](board-layer-rules.ja.md)の「明示指定が主」に従う
- 単独ライブラリとして`libraries/`に置く(ADR-0013の要件: README×2、keywords.txt、example)
- 速度は保証しない。**「動くこと」が仕様で、周波数は成り行き**とする

### 5-1. SoftSPI (**実装済み** 2026-08-29)

```cpp
SoftSPI(uint8_t sck, uint8_t mosi, uint8_t miso = NOT_A_PIN);
```

- `class SoftSPI : public arduino::HardwareSPI`
- `beginTransaction(SPISettings)`でmode 0〜3・bit orderを受ける。**クロック周波数は無視**する
  (`SPISettings`の周波数は上限の宣言であって、下回るのは仕様違反ではない)
- `miso`未指定なら`transfer()`は書き込みのみを行い、読み値は0を返す
- CSは持たない。Arduinoの慣習どおり**スケッチがGPIOとして駆動する**

**実装(`libraries/SoftSPI/`)で仕様から足したもの**:

- `setHalfPeriodUs(uint16_t)` — 半周期の**下限**をµsで置く。既定0(=ループ任せ)。
  長い配線・レベル変換・遅いデバイス向け。`delayMicroseconds`を引き込むので
  **128 Bかかる**(実測)が、bit-bangバスで実際に要求される唯一のつまみなので残した
- `setBitOrder`/`setDataMode`/`setClockDivider` — 旧API。dividerは受け取って無視

CH32V003での実測: base 2404 → SoftSPI **3780**(+1376)。ハードSPIは+1056。

### 5-2. SoftWire (**実装済み** 2026-08-29)

```cpp
SoftWire(uint8_t sda, uint8_t scl);
```

- `class SoftWire : public arduino::HardwareI2C`
- 両padを`OUTPUT_OPENDRAIN`にし、HIGHは解放、LOWは駆動
- **クロックストレッチ対応**: SCLを解放したあと、実際にHIGHになるまで待つ。
  待ち上限は`CH32_WIRE_TIMEOUT_US`(25 ms、ハード実装と同じ既定)
- `setClock()`は半周期の`delayMicroseconds`量に反映する。**達成値ではなく上限**
- **master専用**。`begin(uint8_t address)`はslaveになれないので、
  コンパイルは通し実行時に何もしない(基底が純粋仮想のため実装は必要)
- `endTransmission()`の戻り値はAVR互換(0成功 / 2 アドレスNACK / 3 データNACK / 5 timeout)
- timeout APIはハード実装と同じ形を持つ
  (`setWireTimeout`/`getWireTimeoutFlag`/`clearWireTimeoutFlag`)

**実測(CH32V003)**: base 2404 → SoftWire **4308**(+1904)。ハードWireは+2608。
RAMは+96(ハードは+152)。**ここではソフトのほうが小さい** —
ペリフェラルの状態機械・エラー復旧・route書き込みのほうがbit-bangより高くつく。
SoftSPIとは逆になるので、「ソフトは常に小さい/大きい」とは言えない。

### 5-3. SoftSerial

```cpp
SoftSerial(uint8_t tx);                  /* 送信のみ */
SoftSerial(uint8_t tx, uint8_t rx);      /* 受信つき(条件付き) */
```

- `class SoftSerial : public arduino::HardwareSerial`
- **送信**: `begin(baud)`でビット幅を`1e6/baud`µsとして`delayMicroseconds`で刻む。
  8N1固定。`write()`は**ブロッキング**で、1バイトの間は割り込みしか走らない
- **受信**: EXTIでstart bitの立ち下がりを捉え、ハンドラ内で半ビットずらして
  8回サンプルする。取りこぼしを避けるためリングバッファへ積む。
  - **制約1**: EXTI線はpadのbit番号で決まるので、同じbit番号の他padと排他
  - **制約2**: 1バイトの受信中は割り込みを占有する。ボーレート上限はF_CPU依存で、
    **実測して文書化するまで上限を宣言しない**
  - 受信を作るかは`要判断`。まずTXのみで出す

### 5-4. SoftPWM(条件付き)

- 空きTIMを1本占有し、compare割り込みで任意padをトグルする
- **CH32V003では空きタイマが無いので提供しない**。
  variantが空きTIMを宣言する仕組みが要る(現在は`CH32_TONE_TIMER`/
  `CH32_SERVO_TIMER`しか出していない)
- 優先度は低い。`要判断`

## 6. 検証

- **compile**: 通常のexample sweep(全24 series)に乗る
- **HIL**: `tests/sketches/basic/`に自己検査を置き、**ソフト実装とハード実装を
  同じ2 padに配線して相互に通信させる**。SoftSPI↔SPI、SoftWire↔Wire、
  SoftSerial↔Serial。ソフト側の正しさをハード側が裏書きする形にできる
- `pins_arduino.h`の`CH32_PORT_MASK`がSKU別になったので、
  **選んだpadがそのpackageに無ければ`digitalPinIsValid()`で弾ける**

## 7. 未解決

- SoftSerialの受信を作るか(EXTI線の排他とボーレート上限)
- SoftPWMのための「空きTIM」をvariantに出すか
- WS2812: ハードARGB(V407/V467/X305/X315)を先に見るか、ソフトを先に作るか
- 実装順。**SoftSPI → SoftWire → SoftSerial(TX)** を提案する
  (小さい順で、`HardwareSPI`互換の作法を最初に固める)
- ライブラリを3つ足すと、examples sweepが3 example×24 series分伸びる(約2.6分)。
  許容するか、examplesを1本にまとめるか
- ハードARGB(V407/V467/X305/X315)を別途todoに起こすか
