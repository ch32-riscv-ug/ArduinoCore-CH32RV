# Toolchain

## 正本

- compiler / linker / binutils / libc / runtime / debugger: [ch32rv-toolchain](https://github.com/ch32-riscv-ug/ch32rv-toolchain)
  （clang と lld、LLVM の binutils 相当、picolibc、compiler-rt、libc++、GDB）
- host 別 URL、archive size、SHA-256: `tools/index/tools_ch32rv_toolchain.json`
- C/C++ option と link recipe: `platform.txt`
- series ごとの ISA / ABI: `boards.txt` の生成元

version や host 数はここへ複製しません。採用物を確認するときは上記 machine-readable file を参照します。

## Compiler の条件

- RV32E + ILP32E と、対象 series が必要とする RV32I/M/A/F/C 系を一つの配布系統で扱える
- Linux、macOS、Windows 向け archive を checksum 固定で取得できる
- compiler は上流の LLVM。WCH 固有の命令（XW、LLVM の Xwchc）と属性は、上流にあるもの
  （XW）か、ch32rv-toolchain の `patches/` に置いたものだけを使う
- upstream source と build metadata を追跡できる

CH32V002/V004/V005/V006/V007/M007 の整数乗算は Zmmul で、整数除算命令はありません。
`rv32emc` を指定すると compiler が `div` / `rem` を出して不正命令になるため、これらは
`rv32ec_zmmul_zicsr` / `ilp32e` でビルドします。この ISA 指定は性能選択ではなく実行可否の要件です。

XW（WCH の圧縮命令）は、series のすべての core が持つときだけ付けます（`build.march_xw`）。
持たないのは CH32V103（QingKe V3A）だけです。libc などのライブラリも XW 付きでビルドしてあり、
clang が `-march` / `-mabi` から合う版を選びます（toolchain の `lib/clang-runtimes/multilib.yaml`）。
V103 には XW の無い rv32imac の版を使います。

## Runtime

libc は picolibc、実行時のルーチンは compiler-rt、C++ は libc++ と GNU++17 です。exceptions、RTTI、
thread-safe statics は無効です。picolibc の printf / scanf は整数だけの版を既定にしてビルドしてあります。
printf の系統はコアの書式化で動き、浮動小数の扱い（double / float、`%f`、picolibc の printf / scanf）は
Floating Point のメニュー（`rtlib`）で選びます（[printf と浮動小数](printf-float.ja.md)）。

picolibc が program に任せるもの（`stdin` / `stdout` / `stderr`、`sbrk`、`write`、`_exit`、時刻の
`gettimeofday` / `times`）はコアが定義します（`cores/arduino/ch32rv_stdio_file.c`、`ch32rv_sbrk.c`、
`syscalls.c`）。

最適化は Optimize のメニュー（`opt`）で選びます。既定は `-Oz` と LTO です。

浮動小数から整数への変換で値が範囲の外にあるとき（C では未定義の動作）、FPU の無い系列では compiler-rt の結果になり、
最大値や最小値に丸めません。FPU のある系列（V30x、X315、H41x の float）では命令が丸めます。小さい方を選び、
コアでは揃えません（2026-11-12）。

ホストは Linux（x86_64、arm64）、macOS（Intel、Apple Silicon）、64 bit の Windows です。32 bit の Windows
（xPack の GCC にはあった）は扱いません。

詳細な理由は [ADR-0002](adr/0002-toolchain-distribution.ja.md) と
[ADR-0004](adr/0004-runtime-and-cxx.ja.md) を参照してください。

## 更新規則

toolchain を更新するときは、JSON、package index の依存、生成済み size 基準を同じ変更で更新します。
ch32rv-toolchain は上流の source（LLVM、picolibc、GDB とその依存）から CI でビルドし、GDB とその依存の
source archive をリリースに一緒に載せます（GPL / LGPL）。
