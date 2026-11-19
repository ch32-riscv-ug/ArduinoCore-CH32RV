# SoftPWM

[English](README.md)

どの端子でも PWM を出すソフトウェアの PWM です。API は AVR の定番の SoftPWM ライブラリ（Brett Hagman）と同じです。

```cpp
#include <SoftPWM.h>

void setup() {
  SoftPWMBegin();
  SoftPWMSet(PA0, 64);                    // 64 / 255 が High
  SoftPWMSetFadeTime(PA1, 500, 1000);     // 0→255 を 500 ms、255→0 を 1000 ms で
  SoftPWMSet(PA1, 255);                   // 500 ms かけて明るくなる
}
```

| 関数 | 意味 |
|---|---|
| `SoftPWMBegin(polarity = SOFTPWM_NORMAL)` | タイマーを 1 本取る（`timerBegin()` と同じく番号の大きい空きタイマー）。取れなければ false |
| `SoftPWMSet(pin, value, hardset = 0)` | `value / 255` を High（反転なら Low）。端子は出力になる。フェード中でも `hardset` ですぐに |
| `SoftPWMSetPercent(pin, percent, hardset = 0)` | 0〜100 % で |
| `SoftPWMSetFadeTime(pin, up, down)` | 0→255、255→0 にかける ms。0 はすぐに |
| `SoftPWMSetPolarity(pin, polarity)` | `SOFTPWM_INVERTED` で Low を有効に |
| `SoftPWMEnd(pin)` | 止めて無効のレベルにする。`ALL` は全部止めてタイマーを返す |

- 周波数は `SOFTPWM_FREQ`（既定 60 Hz）、256 段階、端子は `SOFTPWM_MAXCHANNELS`（既定 16）まで。
- 割り込みは `SOFTPWM_FREQ x 256` 回/秒（60 Hz で 15.4 kHz）。縁は割り込みの遅れ（数 µs、ほかの割り込みの間はもっと）だけ動きます。
- 元のライブラリとの違い: 端子の引数は `int8_t` ではなく `int`（このコアでは PC0 以降の番号が 128 以上）。`SoftPWMBegin()` は bool を返す。
- ハードの PWM（`analogWrite()`）が使える端子では、そちらの方が正確で CPU を使いません。

## 試験

signals::test_soft_pwm: 2 本の線に 25 % と 75 % を出し、WireSkein で 60 Hz とデューティを判定。
