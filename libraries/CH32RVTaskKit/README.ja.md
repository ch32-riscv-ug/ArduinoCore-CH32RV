# CH32RVTaskKit

[English](README.md)

起動条件（割り込みからの依頼、期限、周期、条件）があるときだけ動く、短い仕事の部品です。`loop()` から毎回すべての処理を呼ぶ代わりに、「いつ動くか」を仕事ごとに宣言できます。**使わなくてもかまいません。** `loop()` から処理を順に呼ぶ書き方で足りるなら、このライブラリは要りません。

```cpp
#include <CH32RVTaskKit.h>

CH32RVTaskKit::Executor app;
CH32RVSyncKit::Period every250(250);
void blinkStep() { /* 短く処理して戻る */ }
CH32RVTaskKit::Work blink(blinkStep);

void setup() { app.attach(blink); blink.wakeEvery(every250); }
void loop()  { app.runOnce(); }       // 仕事が動くのはここだけ
```

## 考え方

- **仕事は関数と「今動くべきか」だけ。** stack を持たないので、途中で待てません。少し進めて状態を変数に残し、戻ります。ESP32TaskKit や FreeRTOS のタスクは stack を持つので待てますが、ここでは待ちません。その違いが、小さな CH32 で RTOS を使わない理由です。
- **動くのは `runOnce()` の中だけ。** 割り込みや `delay()` の中からは動きません。仕事が動く場所は、`loop()` に書いた `runOnce()` です。
- **1 回の `runOnce()`:** 起動条件を確かめ、準備のできた仕事を登録順に 1 回ずつ動かします。巡回中に頼まれた仕事（自分自身や ISR から）は次の巡回で動き、同じ巡回で 2 回動くことはありません。
- **入れ子にならない。** 仕事の中で `runOnce()` を呼んでも何もせず 0 を返します（別の実行器でも同じ）。
- **時間の保証は無い。** 戻らない仕事があると、すべてが止まります。
- **heap も容量も無い。** 仕事それぞれが自分のリストの節を持ちます。

## 部品

| 部品 | 操作 |
|---|---|
| `Work` | `post()`（どこからでも。ISR も可。何度呼んでも 1 回）、`wakeOn(deadline)`、`wakeEvery(period)`、`wakeWhen(条件の関数)`、`measure(stats)` |
| `Executor` | `attach(work)`（すぐ準備済みになり、最初の `runOnce()` で 1 回動く）、`detach(work)`、`runOnce()`、`anyReady()`、`measure(roundStats)` |
| `WorkStats` | 仕事ごとの `runs`・`totalUs`・`maxUs`（`measure()` したときだけ） |
| `RoundStats` | 実行器の `rounds`・`emptyRounds`・`maxUs` |

期限と周期は [CH32RVSyncKit](../CH32RVSyncKit/README.ja.md) の `Deadline` / `Period` です。

## 時間の数え方の注意

`WorkStats` / `RoundStats` の時間には、途中で動いた割り込みの分も入ります（CPU 時間ではありません）。最大値は見えた値で、保証ではありません。`emptyRounds` は何も動かなかった巡回の数で、CPU が空いていた時間ではありません（`loop()` のほかの処理が動いているかもしれない）。

## サンプル

[examples/README.ja.md](examples/README.ja.md)
