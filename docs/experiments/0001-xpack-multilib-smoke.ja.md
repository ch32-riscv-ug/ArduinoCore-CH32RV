# 実験0001: xPack GCC 14.3.0-1のmultilib確認とcompile smoke

実施日: 2026-08-19
対象question: Q-020, Q-026(一部)、Q-012の前提確認
関連調査: [R-04](../research/toolchain-distributions.ja.md), [R-01](../research/startup-files.ja.md)
実施環境: WSL2 Linux x86_64(実機なし。実行はしていない)

## 目的

R-04で「高確度推定」だったxPack `riscv-none-elf-gcc` 14.3.0-1のmultilib実物と、CH32各coreのmarch/mabiでのcompile/link可否を確認する。

## 入力の固定

- 配布物: `xpack-riscv-none-elf-gcc-14.3.0-1-linux-x64.tar.gz`
- 取得URL: `https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v14.3.0-1/`
- SHA-256: `be1768ef22789f4d9c41384e0261996f51724b84c2efa940d975dd7d9938c726`(配布元.shaファイルと一致確認済み)
- `riscv-none-elf-gcc --version`: `(xPack GNU RISC-V Embedded GCC x86_64) 14.3.0`
- startup/ld: `CH32V006/EVT/EXAM/SRC/Startup/startup_ch32v00X.S`、同`SRC/Ld/Link.ld`(2025-01版EVTミラー、無改変コピー)

## 結果1: multilib実物(`--print-multi-lib`)

rv32部分の全リスト(rv64は省略せず原文はlogにあり):

```
.;                              ← default = rv32imac/ilp32
rv32e/ilp32e   rv32ec/ilp32e   rv32ea/ilp32e
rv32em/ilp32e  rv32eac/ilp32e  rv32emac/ilp32e
rv32i/ilp32    rv32ia/ilp32    rv32im/ilp32    rv32imc/ilp32   rv32iac/ilp32
rv32if_zicsr/ilp32f    rv32ifd_zicsr/ilp32d
rv32iaf_zicsr/ilp32f   rv32iafd_zicsr/ilp32d
rv32imf_zicsr/ilp32f   rv32imfd_zicsr/ilp32d
rv32imafc_zicsr/ilp32f rv32imafdc_zicsr/ilp32d
```

- **rv32e系multilibは6種あり、RV32EC/RV32EmC系の要件を満たす**(R-04の推定を実物で確認)
- defaultコンパイル(`-v`確認)は`-march=rv32imac -mabi=ilp32`

## 結果2: CH32 core別のmultilib選択(`--print-multi-directory`)

| 指定march/mabi | 対象core | 選択されたlib |
|---|---|---|
| rv32ec_zicsr / ilp32e | V2A (CH32V003) | rv32ec/ilp32e(完全一致) |
| rv32emc_zicsr / ilp32e | V2C (CH32V00X系) | **rv32ec/ilp32e**(libはソフト乗算。ユーザコードはハード乗算) |
| rv32imac_zicsr / ilp32 | V3A/V4B/V4C | default(rv32imac/ilp32、完全一致) |
| rv32imc_zicsr_zba_zbb_zbs / ilp32 | V3B | rv32imc/ilp32(libはB命令なし) |
| rv32imafc_zicsr / ilp32f | V4F/V3F/V5F | rv32imafc_zicsr/ilp32f(完全一致) |
| rv32imac_zicsr_zve64x_zvbb / ilp32 | V3V (V407系) | default(vector用libなし、リンク可) |

## 結果3: compile/link smoke(すべて成功)

1. **V00X起動一式**: EVT startup(.S無改変)+最小main.c(SystemInitスタブ+無限ループ)+EVT Link.ld、`-march=rv32emc_zicsr -mabi=ilp32e -Os -nostartfiles -Wl,--gc-sections`
   - `size`: **text 328 / data 0 / bss 516**(bss=counter 4B+gp周辺、stack 512はld定義)
   - ELF header: `Flags: 0x9, RVC, RVE, soft-float ABI`、Entry 0x0
   - section配置: `.init`(vector table 0xa8=168B)が0x0、`.stack`が0x20001e00(=8K RAM末尾-512)→ EVT Link.ldの有効MEMORYは62K/8K版
2. **C++ (ilp32e)**: 仮想関数+グローバルオブジェクトを`g++ -fno-exceptions -fno-rtti -fno-threadsafe-statics`でcompile、`--undefined=cpp_entry`で保持してlink成功。text 348、vtable(`_ZTV4Base`)配置確認。rv32ec/ilp32eのlibディレクトリにnewlib一式(libc/libc_nano/libg/libm/libgloss)を確認
3. **FPU (ilp32f)**: float乗算が`fmul.s`にコンパイルされることを確認
4. **Zve64x+Zvbb**: `vsetvli`を含むinline asmがassemble成功(binutils 2.45系)

## 結論

- xPack riscv-none-elf-gcc 14.3.0-1は、**CH32全familyのISA/ABI要件を単一配布物で満たす**(実物確認済み)
- EVTのstartup/ldは無改変でこのtoolchainを通る(Q-012のprototype作業をこの構成で進められる)
- 未検証のまま残るもの: 実行時挙動(実機なしのため)、printf/浮動小数点formatのサイズと品質(Q-022)、Windows/macOS host、Arduino IDE経由のinstall

## 再現手順

```sh
curl -LO https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v14.3.0-1/xpack-riscv-none-elf-gcc-14.3.0-1-linux-x64.tar.gz
sha256sum -c <(echo "be1768ef22789f4d9c41384e0261996f51724b84c2efa940d975dd7d9938c726  xpack-riscv-none-elf-gcc-14.3.0-1-linux-x64.tar.gz")
tar xzf xpack-riscv-none-elf-gcc-14.3.0-1-linux-x64.tar.gz
BIN=./xpack-riscv-none-elf-gcc-14.3.0-1/bin
$BIN/riscv-none-elf-gcc --print-multi-lib
cp <EVTミラー>/CH32V006/EVT/EXAM/SRC/Startup/startup_ch32v00X.S .
cp <EVTミラー>/CH32V006/EVT/EXAM/SRC/Ld/Link.ld .
printf '#include <stdint.h>\nvoid SystemInit(void){}\nvolatile uint32_t c;\nint main(void){for(;;)c++;}\n' > main.c
$BIN/riscv-none-elf-gcc -march=rv32emc_zicsr -mabi=ilp32e -Os -c startup_ch32v00X.S -o startup.o
$BIN/riscv-none-elf-gcc -march=rv32emc_zicsr -mabi=ilp32e -Os -c main.c -o main.o
$BIN/riscv-none-elf-gcc -march=rv32emc_zicsr -mabi=ilp32e -nostartfiles -T Link.ld -Wl,--gc-sections main.o startup.o -o smoke.elf
$BIN/riscv-none-elf-size smoke.elf
```

## 次の実験候補

- 同手順の全family横断版(W-2のELF検査ハーネスとしてscript化し、CIへ)
- newlib(-nano)のprintfサイズ計測(Q-022)
- Windows/macOSでの同一smoke(GitHub Actionsのmatrixで)
