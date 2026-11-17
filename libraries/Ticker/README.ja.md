# Ticker

[English](README.md)

関数を一定の間隔で、または時間がたってから 1 回だけ呼びます。arduino-esp32 の Ticker の API です。

```cpp
#include <Ticker.h>

Ticker blink;

void setup() {
  pinMode(PA1, OUTPUT);
  blink.attach_ms(500, [] { digitalWrite(PA1, !digitalRead(PA1)); });
}

void loop() {}
```

| 呼び出し | 意味 |
|---|---|
| `attach(seconds, callback)`、`attach_ms(ms, ...)`、`attach_us(us, ...)` | 周期ごとに呼ぶ |
| `once(seconds, callback)`、`once_ms(ms, ...)`、`once_us(us, ...)` | その時間のあと 1 回だけ呼ぶ |
| `... (period, void (*)(T), T arg)` | 同じことを、ポインタ以下の大きさの引数 1 つつきで |
| `detach()` | 止める |
| `active()` | 動いているか（`once` は呼ぶときに止まる） |

コールバックは関数か、ポインタ 2 つまでをキャプチャするラムダです（[CH32RVCallback.h](../../cores/arduino/CH32RVCallback.h)）。ヒープは使いません。

## 動き方

- スケッチの Ticker は全部で 1 つのハードウェアのタイマーを共有します。コアのタイマー API の `timerBegin(1000000)` です（[CH32RVHwTimer.h](../../cores/arduino/CH32RVHwTimer.h)）。最初の Ticker が動き出すときに借り、最後の Ticker が止まると返します。時間はその 64 ビットのカウントの µs です。
- コールバックはそのタイマーの割り込みで動きます（ESP32 では esp_timer のタスク）。短くし、`delay()` や `Serial` の出力などは `loop()` でしてください。コールバックの中で、自分や別の Ticker を attach・detach してかまいません。
- 周期の Ticker は、最初の呼び出しからの周期を保ちます。1 回が遅れても次はずれません。周期をまるごと逃した呼び出しは、2 回呼ばずに飛ばします。
- `analogWrite()`・`tone()`・Servo・ライブラリがタイマーを全部使っていると、`attach*()` / `once*()` は何もせず、`active()` は false です。
- ストップとスタンバイの間はタイマーも止まり、Ticker はその分遅れます。

## 状態

全系列でビルドできます（2026-11-17）。tests/instrumented/signals（`test_ticker`）が、コールバックが反転させる端子をプローブで取り込み、周期と `once_us` を確かめます。

## 例

- [Blinker](examples/Blinker/README.ja.md): arduino-esp32 の例。点滅、点滅の入り切り、1 回だけ遅くする。
