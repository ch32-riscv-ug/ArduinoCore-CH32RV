# ch32fun との大きさの比較

「ch32fun へ逃げられない軽さ」（ADR-0016 の理由）の物差し。
互換は目標にしない。Arduino から入った人が「重いから ch32fun にしよう」と思わない差に保てているかを見る。

## 測り方

- ch32fun は cnlohr/ch32fun の a800cbd。`examples/blink` と、`SystemInit()` だけの空の `main()`。
  `make TARGET_MCU=<MCU> PREFIX=riscv-none-elf <name>.elf`。ch32fun が勧める上流の GCC（xPack GCC 14.3.0-1）でビルド（2026-10-22）。
- このコアは同じ動作の Arduino スケッチ（空の `setup()` / `loop()`、同じ 4 端子を 250 ms ごとに切り替える Blink）を
  既定のメニューでビルド。2026-10-22 は xPack GCC 14.3.0-1、2026-11-12 は ch32rv-toolchain 23.1.5-2（clang、`-Oz`、LTO、XW）。
- Flash は `.text`・`.rodata`・`.data` などの和、RAM は `.data` と `.bss` の和。stack の予約は含まない。

| | ch32fun | このコア（GCC、10-22） | このコア（clang、11-12） | 差（clang） |
|---|---:|---:|---:|---:|
| 空、CH32V003 | 544 B / 0 B | 676 B / 4 B | 640 B / 8 B | +96 B / +8 B |
| 空、CH32X035 | 572 B / 0 B | 716 B / 4 B | 712 B / 12 B | +140 B / +12 B |
| Blink（PD0・PD4・PD6・PC0）、CH32V003 | 692 B / 0 B | 988 B / 4 B | 840 B / 12 B | +148 B / +12 B |

V003 の 16 KB に対して、Blink の差は 0.9 %（GCC では 1.8 %）。clang で RAM が増えたのは、GCC では残らなかった
SysTick の基準の値（`ch32rv_systick_base`、4 B）と、Blink ではスケッチ自身の変数（1 B と、そろえる分）。

## 差の中身（GCC、2026-10-22 の測定）

- **`millis()` の SysTick 割り込み（94 B、RAM 4 B）:** 1 ms ごとの割り込みで時刻を数え、デバッガがクロックを書き換えたら
  `SystemInit()` をやり直す。ch32fun の `Delay_Ms()` は CPU を回して待つだけで、時刻を持たない。
- **`digitalWrite()` / `pinMode()`（`main` が 328 B 対 156 B）:** 端子の番号から port と bit を引き、範囲を確かめる。
  ch32fun の `funDigitalWrite()` はレジスタへ直接書くマクロ。
- **起動（`handle_reset` 218 B、`SystemInit` 186 B）:** `.data` の転送、`.bss` の消去、C++ のコンストラクタ、RCC を既知の状態へ戻す手順。

どれも Arduino の API（`millis()`、端子の番号、C++）か、デバッガ接続時の復帰の分で、使わない機能の固定費ではない。
システムの登録の仕組み（ADR-0016）、CH32RVSyncKit、CH32RVTaskKit は、使わなければこの数字に入らない。

## これから

- 同じ比較を、UART の出力（`Serial` と ch32fun の printf）、割り込み 1 本、K0 の用途で行う。
