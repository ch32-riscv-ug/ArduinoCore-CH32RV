# ch32fun との大きさの比較

ADR-0016 と planning/core-redesign/tasks/ARCH-SELECT-01/decision.ja.md の優先順位 4（ch32fun へ逃げられない軽さ）の物差し。
互換は目標にしない。Arduino から入った人が「重いから ch32fun にしよう」と思わない差に保てているかを見る。

## 測り方（2026-10-22）

- ch32fun は cnlohr/ch32fun の a800cbd（TODO.ja.md の参照と同じ）。`examples/blink` と、`SystemInit()` だけの空の `main()`。
  `make TARGET_MCU=<MCU> PREFIX=riscv-none-elf <name>.elf`。
- このコアは同じ動作の Arduino スケッチ（空の `setup()` / `loop()`、同じ 4 端子を 250 ms ごとに切り替える Blink）を
  既定のメニューでビルド。
- どちらも xPack GCC 14.3.0-1。Flash は `.text`・`.rodata`・`.data` などの和、RAM は `.data` と `.bss` の和。stack の予約は含まない。

| | ch32fun | このコア | 差 |
|---|---:|---:|---:|
| 空、CH32V003 | 544 B / 0 B | 676 B / 4 B | +132 B / +4 B |
| 空、CH32X035 | 572 B / 0 B | 716 B / 4 B | +144 B / +4 B |
| Blink（PD0・PD4・PD6・PC0）、CH32V003 | 692 B / 0 B | 988 B / 4 B | +296 B / +4 B |

V003 の 16 KB に対して、Blink の差は 1.8 %。

## 差の中身

- **`millis()` の SysTick 割り込み（94 B、RAM 4 B）:** 1 ms ごとの割り込みで時刻を数え、デバッガがクロックを書き換えたら
  `SystemInit()` をやり直す。ch32fun の `Delay_Ms()` は CPU を回して待つだけで、時刻を持たない。
- **`digitalWrite()` / `pinMode()`（`main` が 328 B 対 156 B）:** 端子の番号から port と bit を引き、範囲を確かめる。
  ch32fun の `funDigitalWrite()` はレジスタへ直接書くマクロ。
- **起動（`handle_reset` 218 B、`SystemInit` 186 B）:** `.data` の転送、`.bss` の消去、C++ のコンストラクタ、RCC を既知の状態へ戻す手順。

どれも Arduino の API（`millis()`、端子の番号、C++）か、デバッガ接続時の復帰の分で、使わない機能の固定費ではない。
システムの登録の仕組み（ADR-0016）、CH32RVSyncKit、CH32RVTaskKit は、使わなければこの数字に入らない。

## これから

- 同じ比較を、UART の出力（`Serial` と ch32fun の printf）、割り込み 1 本、K0 の用途で行う。
