# CH32RVSyncKit サンプルの使い方

[English](README.md)

割り込みと `loop()` をつなぐ部品の例です。どれも 1 つの部品だけを使い、待たずに戻る書き方を示します。最初は [QueueFromInterrupt](QueueFromInterrupt/README.ja.md) と [CriticalSection](CriticalSection/README.ja.md) から始めます。

## ボードと接続

どの系列でも動きます（CH32V003 を含む）。多くの例は `PA1` と GND の間の押しボタンを割り込み源にします。`PA1` が無い・使えない板では、各 `.ino` の `BUTTON_PIN` を変えます。ログは `Serial` のハードウェア UART へ 115200 bps・8N1 で出します。

| ボード | UART アダプタ |
|---|---|
| `TX` | RX |
| `RX` | TX（入力を使う例） |
| GND | GND |

## 一覧

| 例 | 学ぶこと |
|---|---|
| [QueueFromInterrupt](QueueFromInterrupt/README.ja.md) | 割り込みから `loop()` へ、`Queue` で値を渡します。 |
| [CriticalSection](CriticalSection/README.ja.md) | ISR が書く複数の変数を、矛盾のない 1 組として読みます。 |
| [NotifyCounter](NotifyCounter/README.ja.md) | データ無しで回数だけを伝えます。 |
| [NotifyBits](NotifyBits/README.ja.md) | 種類の違うイベントを 1 つにまとめます。 |
| [BinarySemaphore](BinarySemaphore/README.ja.md) | 「起きた」を 1 回だけ伝えます。 |
| [RingBuffer](RingBuffer/README.ja.md) | 大きさの決まったバイト列です。 |
| [MessageBuffer](MessageBuffer/README.ja.md) | 長さの違うメッセージを丸ごと扱います。 |
| [BlockPool](BlockPool/README.ja.md) | コピーしたくない大きさの記録を、プールのブロックで貸し借りします。 |
| [DeadlinePeriod](DeadlinePeriod/README.ja.md) | `delay()` を使わずに 2 つの予定を同時に進めます。 |
| [TryLock](TryLock/README.ja.md) | `loop()` が何回かに分けて埋めるバッファを、その間 ISR に触らせません。 |

## ビルド

```sh
arduino-cli compile --fqbn ch32-riscv-ug:ch32rv:CH32V003 \
  libraries/CH32RVSyncKit/examples/QueueFromInterrupt
```
