# ADR-0010: ピン番号はポート埋め込みのスパース方式とし、公開名は`PA0`形式にする

- Status: Proposed
- Date: 2026-08-19
- Related questions: Q-011, Q-003

## Context

CH32はマルチポート(PA/PB/PC/PD/PE)で、1パッケージあたりのGPIO数は6(SOP8)から80(LQFP100)まで幅がある。
[ADR-0005](0005-board-structure-and-fqbn.ja.md)で各boardの先頭に`ANY`(型番を知らない利用者向け)を
置くと決めたため、**ピン番号がパッケージに依存するかどうか**が設計に直結する。

## Decision drivers

- `ANY`が成立すること(パッケージごとにpin mapが変わると`ANY`は共通部分しか出せない)
- CH32V003(Flash 16K)でテーブルのコストを払わないこと
- CH32コミュニティの既存表記(`ch32fun`、旧コア)と衝突しないこと
- Arduino標準APIのsignatureを変えないこと

## 調査: 他Arduino coreの方式(2026-08-19、sourceで確認)

| core | 方式 | 実装 |
|---|---|---|
| **STM32duino** | `PAn`マクロ → **パッケージごとの密な連番** | `variants/<family>/<package group>/variant_generic.h`。`PA0`=0…`PA15`=15、`PB0`=16…、**`PC13`=32**(PC0–PC12が非実装なので詰める)。`digitalPin[]`テーブルが要る |
| ESP32 / arduino-pico | **GPIO番号 = ピン番号** | 単一ポートなので自明 |
| `ch32fun` | **ポート埋め込み** | `GpioOf(pin) = GPIOA_BASE + 0x400*(pin>>4)`、bit = `pin & 0xf`(**4bit**) |
| 旧CH32コア | **ポート埋め込み** | `(port<<5) \| bit`(**5bit**) |

**STM32duinoが連番を採れているのは、パッケージごとにvariantを作っているから**である。
`PC13`が32番なのは「そのパッケージでPC0–PC12が出ていないので詰めた」結果で、
別パッケージでは別番号になる。つまり**連番を採ると`ANY`が原理的に作れない**。

## Options considered

### Option A: 密な連番(STM32duino方式)

利点: `digitalPin[]`で高速な配列引きができ、Arduino慣例の`D0`系にも寄せやすい。
欠点: **番号がパッケージ依存**になるためvariantがパッケージ数だけ必要(X035だけで7種)。
`ANY`が成立しない。テーブルがFlashを食う。**不採用**。

### Option B: GPIO番号 = ピン番号(ESP32/RP2040方式)

欠点: CH32はマルチポートなので、そのままでは表現できない。**不採用**。

### Option C: ポート埋め込み・4bit(`ch32fun`方式)

欠点: **CH32X035のPA0–PA23が表現できない**。全CH32のポート別最大ビットを実測したところ
**PA23 / PB21 / PC19**であり、4bitでは足りない。**不採用**。

### Option D: ポート埋め込み・5bit(採用)

```c
/* port = pin >> 5, bit = pin & 31 */
#define PA0  ((0 << 5) | 0)
#define PC13 ((2 << 5) | 13)
```

## Decision

1. ピン番号は**`(port << 5) | bit`のスパース値**とする。5bitなのでPA23等も収まる
2. 公開名は**`PA0`/`PB3`形式**。datasheetおよび回路図の表記と一致させる
3. `digitalWrite`等は`port = pin >> 5`、`bit = pin & 31`でGPIOベースアドレスを算術計算する。
   **pin→padの変換テーブルを持たない**
4. 実在しないpadへの書き込みは**無害**とする(bonded outされていないレジスタビットを叩くだけ)。
   ただし[X035のPC10/PC11](../device-data.ja.md)は内部でPC17/PC16へ結線されているため、
   variant生成で**unusableとして表現する**
5. アナログは`A0`等のエイリアスをADCチャネルへ別途マップする
6. `PINS_COUNT`/`NUM_DIGITAL_PINS`は**連続範囲を意味しない**。0..N-1でループする用途には使えないことを文書化する

## Consequences

- **`ANY` boardが成立する**。pin定義はseriesの全pad名を出しておけばよく、パッケージ別variantが不要
- テーブルが無いためFlash消費がゼロ。CH32V003(16K)で効く
- `ch32fun`・旧コアと同じ表記なので、移植時の読み替えが不要
- **`digitalWrite(13, HIGH)`のような数値直書きのAVRスケッチは動かない**(13はPA13になる)。
  ただし黙って別のピンを叩くよりは明示的に別物である方が安全であり、
  数値直書きは現在の主流ではない。移行表を文書へ用意する
- STM32duinoからの移植では`PA0`表記がそのまま通る(向こうも同じ名前を公開している)

## Validation

- `ANY`を含む全boardのcompile matrix(CI)
- pin計算が定数畳み込みされ、テーブルが生成されないことをELFで確認する(size baseline)
- (実機)代表boardでのGPIO conformance test

## References

- [ADR-0005](0005-board-structure-and-fqbn.ja.md)(series board + ANY)
- STM32duino `variants/STM32F1xx/F103C8T_F103CB(T-U)/variant_generic.h`
- `ch32fun/ch32fun/ch32fun.h`(`GpioOf`)、旧コア`cores/<FAMILY>/Arduino.h`
