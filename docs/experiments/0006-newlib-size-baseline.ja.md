# 実験0006: newlib(-nano)/ilp32eのサイズ実測

実施日: 2026-08-19
対象question: Q-022(runtime構成)、Q-051(size budget)、R-09
実装: [tests/sizebench/](../../tests/sizebench/README.ja.md)
実施環境: WSL2 Linux x86_64、xPack riscv-none-elf-gcc 14.3.0-1(実験0001と同一)。サイズのみ(実行なし)

## 目的

CH32の小容量SKU(V003=16K/2K、V002=16K/4K、V006=62K/8K)を前提に、newlib full / newlib-nanoの代表機能のコストを実測し、default runtime構成(Q-022)の判断材料にする。

## 方法

統合crt0+own sections.ld(計測専用の大きなMEMORY)+最小syscall stubで、代表7ケース × libc(nano/full、%fはnano+`_printf_float`も) × 2 ISA(rv32emc_zicsr/ilp32e、rv32imac_zicsr/ilp32)をビルドし`size`で計測。`-Os -ffunction-sections -fdata-sections -Wl,--gc-sections`、C++は`-fno-exceptions -fno-rtti -fno-threadsafe-statics`。

## 結果(rv32ec=ilp32e側を抜粋。全表はsizebenchで再現可能)

| case | libc | text | data | bss | 備考 |
|---|---|---:|---:|---:|---|
| 空main(crt0のみ) | − | 360 | 0 | 512 | bss=stack 512 |
| C++ virtual | nano/full同一 | 392 | 8 | 516 | **+32Bで実質ゼロ** |
| C++ new/delete | nano | 1,212 | 92 | 540 | malloc込みで実用可 |
| C++ new/delete | **full** | **114,084** | 1,932 | 1,084 | **62K SKUに収まらない** |
| puts | nano | 3,084 | 96 | 852 | |
| puts | full | 8,676 | 1,384 | 896 | |
| printf %d | nano | 4,932 | 100 | 852 | |
| printf %d | **full** | **47,952** | 1,856 | 896 | 16K SKU不可、62Kでも非現実的 |
| snprintf %d | nano | 3,364 | 92 | 532 | |
| printf %f | nano(そのまま) | 5,476 | 100 | 852 | **%fは出力されない**(nanoの仕様) |
| printf %f | nano+`-u _printf_float` | 24,948 | 552 | 852 | +19.5KB。16K SKU不可、62Kなら可 |
| printf %f | full | 48,136 | 1,856 | 896 | |

- rv32imac/ilp32との差は±数%(fullのnewが109,664等)で、ISAによる傾向差はない
- nanoのstdio系RAMコスト: data約100B+bss約340B(計約450B)。**V003(RAM 2K)ではstdioを使うだけでRAMの2割超**
- fullのnew/deleteが114KBに膨らむのは、operator newの失敗経路(verbose terminate handler)がfull stdio一式を引き込むため(objdump確認は簡易。詳細分解は必要になったら)

## 結論(Q-022への提案)

1. **default runtimeはnewlib-nano(`--specs=nano.specs`)一択**。fullはprintfで47KB、C++ newで114KBとなり、対象SKU群では成立しない
2. `printf`系の`%f`はデフォルト無効とし、**menuでのopt-in**(`-u _printf_float`、+19.5KB)にする(STM32duinoのC Runtime menuと同型)
3. **C++言語機能そのもの(virtual、new/delete)はnanoなら軽く**、Arduino APIのC++設計に制約を与えない
4. コアの`Serial.print`等はnewlibのprintfに依存しない実装(ArduinoCore-APIのPrint系)とし、printf系のコストは「ユーザーが直接呼んだ場合」に限定する
5. V003(2K RAM)ではstdioのRAM常駐コストが重く、printf系を使わない前提のプロファイルが必要

## 再現手順

```sh
CH32_GCC_BIN=<xpack-riscv-none-elf-gcc-14.3.0-1>/bin \
tests/sizebench/run_sizebench.sh /tmp/sizebench
# /tmp/sizebench/results.md にMarkdown表が出力される
```

## 残る未検証事項

- ch32fun同等sketchとの比較(toolchain認定matrixの本番。代表sketch確定後)
- `-flto`の効果、`-Og/-O2`との比較
- malloc実装の差し替え(nanoのmalloc vs 自前)とheap断片化方針
- 実行時の正しさ(printf出力内容の確認は実機/エミュレーションで)
