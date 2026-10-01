# ADR-0010: ピン番号はポート埋め込みのスパース方式とし、公開名は`PA0`形式にする

- Status: Accepted(2026-10-01、ユーザー判断。製品boardの印刷番号と port 欄のずらしを追加)
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
/* port = (pin >> 5) - 2, bit = pin & 31 (2026-10-01 に port 欄を 2 ずらした) */
#define PA0  (((0 + 2) << 5) | 0)    /* 64 */
#define PC13 (((2 + 2) << 5) | 13)   /* 141 */
```

## Decision

1. ピン番号は**`((port + 2) << 5) | bit`のスパース値**とする。5bitなのでPA23等も収まる。
   port 欄を 2 から始めるので pad は 64〜254 に入り(`pin_size_t` は `uint8_t`、`0xFF` は `NOT_A_PIN`)、
   **0〜63 は pad を指さない**(7 を参照)
2. 公開名は**`PA0`/`PB3`形式**。datasheetおよび回路図の表記と一致させる
3. `digitalWrite`等は`port = pin >> 5`、`bit = pin & 31`でGPIOベースアドレスを算術計算する。
   **pin→padの変換テーブルを持たない**
4. 実在しないpadへの書き込みは**無害**とする(bonded outされていないレジスタビットを叩くだけ)。
   ただし[X035のPC10/PC11](../device-data.ja.md)は内部でPC17/PC16へ結線されているため、
   variant生成で**unusableとして表現する**
5. アナログは`A0`等のエイリアスをADCチャネルへ別途マップする
6. `PINS_COUNT`/`NUM_DIGITAL_PINS`は**どの pin 番号もこれより小さい、という上限**とする(arduino-esp32 の
   `SOC_GPIO_PIN_COUNT` と同じ意味)。番号は飛び飛びなので 0..N-1 には欠番があり、有効かどうかは
   `digitalPinIsValid()` で見る。pad の数は `CH32RV_GPIO_COUNT`
7. **基板に番号が印刷されている製品boardに限り**、その番号を 0〜63 で使える。variant が
   `CH32RV_BOARD_PIN_COUNT` と `CH32RV_BOARD_PINS { 0 番の pad, 1 番の pad, ... }` を置き、core は公開 API の
   入口で 1 回だけ `CH32RV_PIN_RESOLVE()` で pad に置き換える。pad 名(`PA1`)と `D` 名はどの board でも
   pad の値のままで、数字の `0` と `PA1` が同じ pad を指す。番号から pad を引く公開名は
   `digitalPinToGPIONumber()`(arduino-esp32 の Nano ESP32 と同じ意味)。印刷番号の無い pad は番号を作らず
   pad 名で使う。印刷が pad 名の board(WCH の EVT 等)と Generic は表を持たず、0〜63 は無効。
   番号の意味を切り替えるメニュー(arduino-esp32 の Nano ESP32 の Pin Numbering)は置かない:
   数字を直に使う下の層が無く、pad 名は常に同じ pad を指すため

## Consequences

- **`ANY` boardが成立する**。pin定義はseriesの全pad名を出しておけばよく、パッケージ別variantが不要
- テーブルが無いためFlash消費がゼロ。CH32V003(16K)で効く
- `ch32fun`・旧コアと同じ表記なので、移植時の読み替えが不要
- **Generic では`digitalWrite(13, HIGH)`のような数値直書きは何もしない**(0〜63 は無効)。
  黙って別のピンを叩くより安全であり、数値直書きは現在の主流ではない。移行表を文書へ用意する。
  印刷番号のある製品board(UIAPduino の 0〜17)では数値直書きがその番号の端子に届く
- 製品boardの表は数字で指定したときだけ引かれる。pad 名なら定数畳み込みで表に触れない。
  2026-10-01 の実測で、UIAPduino(V003)は以前の比較式による置き換えより 28〜532 byte 小さく、
  Generic は port 欄のずらしの分だけ 12〜132 byte 大きい
- STM32duinoからの移植では`PA0`表記がそのまま通る(向こうも同じ名前を公開している)

## Validation

- `ANY`を含む全boardのcompile matrix(CI)
- pin計算が定数畳み込みされ、テーブルが生成されないことをELFで確認する(size baseline)
- (実機)代表boardでのGPIO conformance test

## References

- [ADR-0005](0005-board-structure-and-fqbn.ja.md)(series board + ANY)
- STM32duino `variants/STM32F1xx/F103C8T_F103CB(T-U)/variant_generic.h`
- `ch32fun/ch32fun/ch32fun.h`(`GpioOf`)、旧コア`cores/<FAMILY>/Arduino.h`
