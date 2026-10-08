# Toolchain

## 正本

- compiler / binutils / newlib: xPack `riscv-none-elf-gcc`
- host 別 URL、archive size、SHA-256: `tools/index/tools_xpack_gcc.json`
- C/C++ option と link recipe: `platform.txt`
- series ごとの ISA / ABI: `boards.txt` の生成元

version や host 数はここへ複製しません。採用物を確認するときは上記 machine-readable file を参照します。

## Compiler の条件

- RV32E + ILP32E と、対象 series が必要とする RV32I/M/A/F/C 系を一つの配布系統で扱える
- Linux、macOS、Windows 向け archive を checksum 固定で取得できる
- compiler fork 固有の命令や attribute を core の前提にしない
- upstream source と build metadata を追跡できる

CH32V002/V004/V005/V006/V007/M007 の整数乗算は Zmmul で、整数除算命令はありません。
`rv32emc` を指定すると GCC が `div` / `rem` を出して不正命令になるため、これらは
`rv32ec_zmmul_zicsr` / `ilp32e` でビルドします。この ISA 指定は性能選択ではなく実行可否の要件です。

## Runtime

既定は newlib-nano と GNU++17 です。exceptions、RTTI、thread-safe statics は無効です。
`printf` / `scanf` の浮動小数点変換は `rtlib` menu で必要なものだけ追加します。

詳細な理由は [ADR-0002](adr/0002-toolchain-distribution.ja.md) と
[ADR-0004](adr/0004-runtime-and-cxx.ja.md) を参照してください。

## 更新規則

toolchain を更新するときは、JSON、package index の依存、生成済み size 基準を同じ変更で更新します。
上流 archive を再梱包しないことを既定とし、再配布が必要になった場合は license、対応 source、改変内容を
明示して別の設計判断を行います。
