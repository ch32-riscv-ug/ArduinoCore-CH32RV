# CH32RVSyncKit

[English](README.md)

割り込みと `loop()` をつなぐ小さな部品の集まりです。Queue、通知（カウンタ・ビット）、バイナリセマフォ、バイト列・メッセージのバッファ、ブロックのプール、短いロック、割り込み禁止区間、期限と周期。RTOS を使わずに、1 つのコアの上で「割り込みは合図だけ、処理は通常側」を書くためのものです。

```cpp
#include <CH32RVSyncKit.h>

CH32RVSyncKit::Queue<uint32_t, 8> presses;      // ISR と共有する 8 個入りの Queue

void onPress() { presses.trySend(millis()); }   // ISR: 入れてすぐ戻る

void loop() {
  uint32_t t;
  while (presses.tryReceive(t)) {               // 届いた分を取り出して先へ進む
    Serial.println(t);
  }
}
```

## 考え方

- **どの操作も待たない。** 1 回試して戻ります。満杯・空・取られている、は「今は無理」という答えで、呼んだ側が状態を持って後でもう一度試します。`receive(timeout)` のような待つ版はありません。同じコアの上で待つと、その間ほかの仕事がすべて止まるからです。
- **ISR と共有するかを、宣言で選ぶ。** テンプレート引数の 2 つ目（または 3 つ目）で決めます。
  - `IsrSafe`（既定）: 各操作の数命令の間だけこのコアの割り込みを止めます。ISR と `loop()` の両方から使えます。CH32V003 には原子命令が無いので、これが唯一の方法です。
  - `LoopOnly`: 割り込みを止めません。`loop()` と、そこから呼ぶ関数だけで使うとき。小さく速くなります。ISR から触ると壊れます。
- **使わなければ 0 バイト。** すべてヘッダーだけのテンプレートで、宣言しない部品はリンクされません。heap も使いません。
- **1 つのコアの中だけ。** CH32H41x のもう 1 つのコアは、こちらが割り込みを止めても動き続けます。コア間のやり取りには使いません。

## 部品

| 部品 | 主な操作 | 用途 |
|---|---|---|
| `Queue<T, N>` | `trySend`、`trySendToFront`、`tryReceive`、`tryPeek`、`count`、`clear`、`overwrite`（`N == 1` のみ） | 値を順番どおりに渡す。満杯は新しい値を断る |
| `Notify<>` | `notify`、`tryTake`、`tryTakeAll`／`setBits`、`tryWaitBits(mask, clearOnExit, waitAll)` | 回数だけ、または種類（ビット）だけを伝える |
| `BinarySemaphore<>` | `give`、`tryTake` | 「起きた」を 1 回。取り出す前の 2 回目の `give` は false |
| `Ring<N>` | `write`（入る分だけ）、`read`、`available`、`space` | バイト列 |
| `MessageBuffer<N>` | `trySend`（全部か無しか）、`tryReceive`、`nextLength`、`count` | 長さの違うメッセージ。1 件で長さ + 2 バイト |
| `BlockPool<T, N>` | `tryAllocate`、`release`（二重返却・他のポインタは false）、`available` | 大きな記録を貸し出す。`Queue<T *, N>` と組む |
| `TryLock<>` | `tryLock`、`unlock`、`Guard` | 資源を少しの間占有する。誰も待たない |
| `CriticalSection` | スコープの間、割り込みを止める（入れ子可） | ISR が書く複数の変数を 1 組として読む |
| `Deadline` | `arm(ms)`、`expired`、`remaining`、`disarm` | タイムアウト、再試行 |
| `Period` | `due`、`missed`、`start` | 周期。遅れた分はまとめて実行せず、飛ばした数を数える |

`Deadline` と `Period` は `millis()` を読みます（`now` を渡す版もあります）。どちらも `loop()` の速さ以上には正確になりません。

## 割り込み禁止区間の注意

`CriticalSection` と `IsrSafe` の各操作は、その間このコアのすべての割り込み（USB、USB PD、UART、SysTick）を待たせます。

- 中ではコピーだけを行い、表示や計算は外で行う。
- 中で `delay()`、`Serial.print()`、待ちのループを使わない。
- ISR の中では割り込みはすでに止まっているので、ISR 側で `CriticalSection` は要らない（あっても害はない）。

## ESP32SyncKit との対応

名前は [ESP32SyncKit](https://github.com/tanakamasayuki/ESP32SyncKit) に合わせています。ESP32 で同じ書き方を続けられます。

| ESP32SyncKit | CH32RVSyncKit |
|---|---|
| `Queue<T> q(depth)`、`send(v, timeout)`／`trySend(v)` | `Queue<T, N> q`、`trySend(v)` のみ。容量はテンプレート引数 |
| `Notify`：`notify`／`take`／`takeAll`／`setBits`／`waitBits` | `Notify<>`：`notify`／`tryTake`／`tryTakeAll`／`setBits`／`tryWaitBits` |
| `BinarySemaphore`：`give`／`take(timeout)` | `BinarySemaphore<>`：`give`／`tryTake` |
| `Mutex`＋`LockGuard` | タスクが無いので無し。`TryLock<>`＋`Guard`、`CriticalSection` |
| ISR かどうかを自動判定 | `IsrSafe`／`LoopOnly` を宣言で選ぶ |

## サンプル

[examples/README.ja.md](examples/README.ja.md) にあります。

## ライセンス

コアと同じです。
