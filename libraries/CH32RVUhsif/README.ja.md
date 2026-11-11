# CH32RVUhsif

[English](README.md)

CH32H415 / H416 / H417 の UHSIF（高速の並列インターフェース）を使うライブラリです。UHSIF は、クロックごとに動く状態機械で 16 ビットのデータ線を扱い、socket の DMA で RAM に書きます。WCH はレジスタを公開しておらず、バイナリのライブラリ（`libUHSIF.a`）だけを配っています。このライブラリはそれを使わず、実機で測った結果から作りました（調べた記録は [docs/h417-uhsif.ja.md](../../docs/h417-uhsif.ja.md)）。

ロジックアナライザとして取り込むだけなら、この上に作った [CH32RVLogicCapture](../CH32RVLogicCapture/README.ja.md) の `begin(UHSIF_DATA, rate)` が簡単です。

## できること（CH32H417 で確かめたこと、2026-11-11）

- データ線の取り込み。クロックは内部の SYSCLK ÷ (divider + 1)（400 MHz ÷ 1〜64）。
- 状態機械。状態は 255 個までで、出口は左右の 2 つです。右へ進む条件（真理値表）は 16 通りまで書けます。
- データのカウンター。
- スレッド 4 つ。どれも、バッファーの輪に CPU 抜きで書き続けます。
- 状態機械からの割り込み。

まだ扱わないもの: バスへの出力、外部のクロック、制御線の出力、8 ビット・32 ビット幅。

## 端子

| データ線 | 端子 |
|---|---|
| DATA0〜5 | PD10〜PD15 |
| DATA6〜8 | PF0〜PF2 |
| DATA9〜12 | PC6〜PC9 |
| DATA13〜15 | PA13〜PA15 |

端子のモードに関係なく読めます（入力、別の機能、GPIO の出力のどれでも）。`begin()` は端子に触りません。入力の電圧は、その端子の電源の電圧までです。

## 使い方

```cpp
#include <CH32RVUhsif.h>

CH32RVUhsif uhsif;
static uint16_t ring[8192] __attribute__((aligned(16)));

uhsif.begin(63);                                  // SYSCLK / 64
uhsif.setStates(states, 2, start);                // 状態機械
uhsif.setDataCounter(2048 - 2);                   // バッファー 1 つ分で上限
uhsif.setRing(0, ring, 4096, 4, 4096);            // スレッド 0: 4096 バイト × 4
uhsif.onInterrupt([] { /* 状態機械の Interrupt */ });
uhsif.start();
```

全体は例の [UhsifStates](examples/UhsifStates/README.ja.md) にあります。

### 状態と遷移

- 各クロックで、状態は `goRight` が真なら右の遷移で、偽なら左の遷移で出ます。
- `goRight` は、状態の 4 つの入力 a〜d の真理値表です（bit (a + 2b + 4c + 8d)）。0 なら、いつも左です。
- 遷移には、入る先の状態と、そのときの出力（alpha と beta）を書きます。
- `start()` は、`start` の遷移で最初の状態に入ります。

| 出力 | 意味 |
|---|---|
| `SampleData`（alpha） | データ線をラッチする |
| `Push` | ラッチした値を、そのスレッドの socket に渡す |
| `CountData` / `ResetDataCount` | データのカウンターを +1 / 0 にする |
| `Interrupt` | `onInterrupt()` のコールバックを呼ぶ |
| `thread(n)` | どのスレッドに渡すか（0〜3） |

| 入力 | 意味 |
|---|---|
| `Ctrl0` + n | 制御線 CTRLn（測っていない） |
| `DataCountHit` | データのカウンターが上限にいる |
| `DmaReady` | 今のスレッドの socket が受け取れる |
| `One` | いつも 1 |

ハードウェアの決まり（測った結果）:

- 同じ状態へ戻る遷移では、alpha はもう出ません。毎クロック取り込むには、2 つの状態を行き来させます。
- `ResetDataCount` は、その遷移で入った状態の判断にはまだ効きません。リセットで入る状態は、カウンターを見ずに左へ出るようにします。
- データのカウンターの上限は「1 つのバッファーのサンプル数 − 2」にします（8192 サンプルなら 8190）。

### バッファーの輪

- `setRing(n, first, bytes, count, stride)` で、スレッド n は `count` 個のバッファー（k 番目は `first + k × stride`）を順に書き、輪になって続けます。
- `bytes` は 16 の倍数で、65520 まで。バッファーは DMA の届く RAM（グローバルや static の配列）に、16 バイト境界で置きます。
- socket は、1 つのバッファーを書き終えて次の descriptor を読むまで、約 150 ns 受け取れません（200 MHz で約 30 クロック）。1 スレッドでは、その間のサンプルが抜けます。
- 抜けを無くすには、2 つのスレッドを交互に使います。片方が次を読む間、もう片方が書きます。CH32RVLogicCapture の UHSIF の取り込みがこの形です。

### 止める

`stop()` は UHSIF をリセットして止めます。設定（`begin()`、`setStates()`、`setDataCounter()`、`setRing()`）も消えるので、もう一度 `start()` する前に設定し直してください。バッファーの中身は残ります。

## エラー

操作は `bool` を返し、理由は `lastError()` と `lastErrorName()` で読めます。

| `CH32RVUhsifError` | 意味 |
|---|---|
| `NotBegun` | `begin()` の前 |
| `InvalidArgument` | 番号、大きさ、位置（16 バイト境界）のどれかが違う |
| `TooManyFunctions` | `goRight` の真理値表が 16 通りを超えた |
| `Running` | 動いている間は設定を変えられない（先に `stop()`） |

## そのほか

- UHSIF は 1 つなので、同時に `begin()` できるのは 1 つのオブジェクトだけです。
- SRAM へは、2 スレッドで 400 MHz（800 MB/s）まで、抜けずに流れました。ただ 400 MHz では、390 kHz の PWM の 1 周期が 1 か所で 2 つに割れました（端の近くを取ったときの乱れとみています）。200 MHz 以下では割れませんでした。
- RM の UHSIF の仕様は 125 MHz までです。速い信号での確かさ（セットアップとホールド、16 本のずれ）は、まだ外の信号源で見ていません。
- 1 つのバッファーごとに割り込みを出すときは、割り込みの間隔を 20 µs 以上にしてください。200 MHz で 256 サンプルごとにしたら、CPU が割り込みの処理だけになりました。

## 例

- [UhsifStates](examples/UhsifStates/README.ja.md): 状態機械を書いて、データ線を毎クロック取り込み、バッファーの数を数える。
