# CH32RVSPISlave

[English](README.md)

SPI をスレーブ（周辺機器側）として使います。マスターは別のチップで、クロックはマスターが出します。転送は CS 1 回分です。CS が下がってから上がるまでに、キューに入れたバイトを送り、マスターから来たバイトを受け取ります。

```cpp
#include <CH32RVSPISlave.h>

CH32RVSPISlave slave;
static uint8_t tx[4] = {0xA5, 0x01, 0x02, 0x03};
static uint8_t rx[4];

void setup() {
  slave.begin(PA5, PA6, PA7, PA4);      // SCK, MISO, MOSI, CS（CS は好きな端子）
  slave.queue(tx, rx, sizeof rx);       // 次の CS に備える
}

void loop() {
  CH32RVSPISlave::Result r;
  if (slave.poll(r)) {                  // CS が 1 回終わった
    // マスターが r.length バイト送った。rx には先頭の min(r.length, 4) バイト
    slave.queue(tx, rx, sizeof rx);     // 次の CS に備える
  }
}
```

## 転送の流れ

- **先にキューに入れます。** `queue(tx, rx, size)` で転送を 1 つ用意します。4 つまで順番に待てます。`tx` の 1 バイト目は、CS が下がる前に SPI に入っています。
- **長さはマスターが決めます。** `size` より多く送られたときは、`tx` のあとに埋め草のバイト（`setFill()`、既定は 0xFF）を送ります。受け取るのは `size` バイトまでです。`Result.length` には送られた全部の数が入ります。
- **キューが空のとき:** 埋め草のバイトを送り、何も受け取らず、`missed()` に数えます。
- **クロックの無い CS のパルス** では、キューを使いません。
- **MISO は CS が上がっている間は手放します**（入力にします）。複数のスレーブで同じバスを使えます。

## タイミング

CS は割り込み付きの普通の端子で、SPI の NSS 端子ではありません。バイトも 1 つずつ割り込みで運びます。そのため、マスターに少し待ってもらう必要があります。

- CS を下げてから、最初のクロックまで。
- CS を上げてから、次に CS を下げるまで。
- 1 バイトごと: 次のバイトが届く前に割り込みが走る必要があります。間に合わなかったバイトは `overruns()` に数えます。

2026-10-28 に [SPISlaveSelfTest](examples/SPISlaveSelfTest/README.ja.md)（同じチップの GPIO で自分の端子を叩く）で測りました。

- モード 0〜3 と LSB 先頭が、次の系列で通りました: CH32V003（24 MHz）、V103、V307、X035、L103。クロックは約 100〜200 kHz です。
- CH32V003 は、CS を下げてからクロックまで 2 µs（ほかに `digitalWrite()` 1 回分）でも通りました。
- ハードウェアのマスターで出せる上限のクロックは、まだ測っていません。

## API

| 呼び出し | 意味 |
|---|---|
| `begin(sck, miso, mosi, cs, mode = SPI_MODE0, order = MSBFIRST)` | SCK / MISO / MOSI は、`SPI.begin(sck, miso, mosi)` と同じく、SPI のどれか 1 つの route でなければなりません。違うとき、その SPI が使用中のときは false |
| `end()` | キューを捨て、端子と SPI を返す |
| `queue(tx, rx, size)` | 転送を 1 つ用意する。`tx` が nullptr なら埋め草のバイト、`rx` が nullptr なら受け取らない。4 つ待っていると false（終わって取り出していないものも数える） |
| `transfer(tx, rx, size, timeout_ms = 0)` | `queue()` して終わりを待つ。送られたバイト数を返し、タイムアウトなら 0。ほかに待っているものが無いときだけ使える |
| `poll(result)` | いちばん古い、終わった転送。`rx`、`length`（送られた数）、`size` |
| `onTransfer(callback, arg)` | または、終わった転送ごとにコールバックを呼ぶ。呼ぶのは `loop()` の `deliverEvents()` |
| `setFill(byte)` | `tx` のあとと、キューが空のときに送るバイト |
| `numTransactionsInFlight()` / `available()` | 用意して終わっていない数 / 終わって取り出していない数 |
| `missed()` / `overruns()` | キューが空だった CS の数 / 割り込みが遅れて失ったバイトの数 |

`SPI`（バスのオブジェクト）が使っている SPI は、同時にスレーブにできません。SPI が 1 つだけのボードでは、両方を開かないでください。

## 系列

SPI のある全系列で使えます。LSB 先頭は、ソフトでビットを並べ替えます。CH32V003 と CH32X035 では、SPI 自身の LSB 先頭の設定でスレーブを動かすと、各バイトの最初のビットを 8 回送ってしまうためです。

## 例

- [SPISlaveSelfTest](examples/SPISlaveSelfTest/README.ja.md): マスターも配線も無しでスレーブを確かめる。
