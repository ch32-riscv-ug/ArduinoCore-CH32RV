# CH32RVDma

[English](README.md)

DMA のチャネルで、メモリからメモリへの写し（copy）と塗りつぶし（fill）をします。終わるまで待つ形と、裏で動かして
CPU はほかの仕事を続け、終わったらコールバックを呼ぶ形があります。全系列で使えます。

```cpp
#include <CH32RVDma.h>
CH32RVDma dma;
dma.begin();                          // 空いているチャネルを 1 つ取る
dma.copy(dst, src, n);                // 写し終わるまで待つ
dma.onDone(done, arg);                // 裏の仕事が終わるたびに done(arg)（チャネルの割り込みの中）
dma.copyAsync(dst, src, n);           // すぐ戻る。busy() / wait()
dma.fill(dst, 0, n);                  // n バイトを 1 つの値で
```

## API

| 関数 | 内容 |
|---|---|
| `begin()` / `end()` / `isAttached()` | チャネルを取る・返す。取れるのは、ほかが持っていない（`ch32rv_dma_claim()`）うえに、割り込みのハンドラーがリンクされていないチャネル。最後のチャネルから探す（先頭のチャネルは周辺の決まった要求: Serial の DMA バッファ、CH32RVAdcStream、SD_MMC）。無ければ false |
| `dma()` / `channel()` | 取った DMA とチャネルの番号（取る前は 0） |
| `copy(dst, src, bytes)` / `fill(dst, value, bytes)` | 終わるまで待つ。`begin()` の前、仕事の途中、0 バイトは false で何もしない |
| `copyAsync(dst, src, bytes)` / `fillAsync(dst, value, bytes)` | 始めてすぐ戻る。その間 CPU は自由 |
| `onDone(cb, arg)` | 裏の仕事が終わったら、チャネルの割り込みの中で `cb(arg)`。`nullptr` で無し。待つ形（`copy()` / `fill()`）では呼ばない |
| `busy()` / `wait()` / `abort()` | 動いているか、終わるまで待つ（割り込みを止めた中からでも進める）、止める（動かした分はそのまま） |

- アドレス 2 つと長さがどれも 4 の倍数なら語（32 bit）、2 の倍数なら半語、それ以外はバイトで動かします。
- 長い転送は 65535 単位（CNTR の 16 bit）ずつに分け、次の分を割り込みから始めます。
- 速さ: CPU の `memcpy()` の方が速い石があります（4 KiB の写し、2026-11-19: X035 は memcpy 218 µs・DMA 140 µs、V203 は
  24・44 µs、H417 は 28・53 µs）。DMA の写しが得になるのは、その間 CPU に別の仕事があるときです。
- スケッチが自分で WFI（`__asm__("wfi")`）を使って待つと、V003・V203・X035 では眠っている間 DMA が止まります（V006・L103 は
  進む。2026-11-20）。`wait()` は WFI を使いません。
- 割り込みは `ch32rv_irq_attach()`（[ch32rv_priority.h](../../cores/arduino/ch32rv_priority.h)）で実行時に付けます。V103 は
  ベクタ表が命令の並びで、リンクされたハンドラーを見分けられないので、そのチャネルを使うライブラリ（CH32RVLogicCapture
  など）と同時に使うと、そのチャネルでは `onDone()` が呼ばれないことがあります（`busy()` / `wait()` は動きます）。

## 例

| 例 | 内容 |
|---|---|
| [DmaCopy](examples/DmaCopy/README.ja.md) | 写しの間に CPU が数え続け、終わりをコールバックで知る。塗りつぶしも確かめる |

試験: selfapi の `test_dma`（ずれたバイトの写し、語の写し、塗りつぶし、裏の写しとコールバック、65535 を超える分割）。
