#!/usr/bin/env bash
# W-2 PoC: build EVT-startup and unified-startup ELFs for 3 families and verify equivalence.
# Requires: CH32_MIRROR_ROOT (EVT mirrors), CH32_GCC_BIN (riscv-none-elf-gcc bin dir)
set -euo pipefail

: "${CH32_MIRROR_ROOT:?set CH32_MIRROR_ROOT to the directory containing CH32V006/ CH32X035/ CH32V307/}"
: "${CH32_GCC_BIN:?set CH32_GCC_BIN to the xPack riscv-none-elf-gcc bin directory}"
WORK="${1:?usage: run_check.sh <workdir>}"
HERE="$(cd "$(dirname "$0")" && pwd)"
GCC="$CH32_GCC_BIN/riscv-none-elf-gcc"

mkdir -p "$WORK"
cd "$WORK"

# Test program: SystemInit stub, one overridden ISR, endless main.
cat > main.c <<'EOF'
#include <stdint.h>
void SystemInit(void) {}
volatile uint32_t counter;
void SysTick_Handler(void) { counter = 1; }
int main(void) { for (;;) counter++; }
EOF

# Vector specs are generated from the local EVT mirrors and MUST NOT be committed.
python3 "$HERE/extract_vectors.py" "$CH32_MIRROR_ROOT/CH32V006/EVT/EXAM/SRC/Startup/startup_ch32v00X.S" vectors_v00x.inc
python3 "$HERE/extract_vectors.py" "$CH32_MIRROR_ROOT/CH32X035/EVT/EXAM/SRC/Startup/startup_ch32x035.S" vectors_x035.inc
python3 "$HERE/extract_vectors.py" "$CH32_MIRROR_ROOT/CH32V307/EVT/EXAM/SRC/Startup/startup_ch32v30x_D8C.S" vectors_v307d8c.inc

# The V00X EVT linker script has no .vector output section; add one for the unified build.
python3 - "$CH32_MIRROR_ROOT" <<'EOF'
import sys
root = sys.argv[1]
src = open(root + '/CH32V006/EVT/EXAM/SRC/Ld/Link.ld').read()
open('ld_v00x.ld', 'w').write(src)
open('ld_v00x_vector.ld', 'w').write(src.replace(
    '} >FLASH AT>FLASH',
    '} >FLASH AT>FLASH\n\t.vector : { *(.vector); . = ALIGN(64); } >FLASH AT>FLASH', 1))
EOF
cp "$CH32_MIRROR_ROOT/CH32X035/EVT/EXAM/SRC/Ld/Link.ld" ld_x035.ld
cp "$CH32_MIRROR_ROOT/CH32V307/EVT/EXAM/SRC/Ld/Link.ld" ld_v307.ld

build() { # tag march mabi startup-extra-args startup ld
  local tag=$1 march=$2 mabi=$3 args=$4 startup=$5 ld=$6
  $GCC -march="$march" -mabi="$mabi" -Os -c main.c -o "main_$tag.o"
  # shellcheck disable=SC2086
  $GCC -march="$march" -mabi="$mabi" -Os -I. $args -c "$startup" -o "startup_$tag.o"
  $GCC -march="$march" -mabi="$mabi" -nostartfiles -T "$ld" -Wl,--gc-sections \
       "main_$tag.o" "startup_$tag.o" -o "$tag.elf"
}

build evt_v00x rv32emc_zicsr  ilp32e "" "$CH32_MIRROR_ROOT/CH32V006/EVT/EXAM/SRC/Startup/startup_ch32v00X.S" ld_v00x.ld
build evt_x035 rv32imac_zicsr ilp32  "" "$CH32_MIRROR_ROOT/CH32X035/EVT/EXAM/SRC/Startup/startup_ch32x035.S" ld_x035.ld
build evt_v307 rv32imafc_zicsr ilp32f "" "$CH32_MIRROR_ROOT/CH32V307/EVT/EXAM/SRC/Startup/startup_ch32v30x_D8C.S" ld_v307.ld

build uni_v00x rv32emc_zicsr  ilp32e '-DCH32_VECTORS="vectors_v00x.inc" -DCH32_MSTATUS_INIT=0x1880 -DCH32_INTSYSCR_INIT=0x3' "$HERE/crt0_ch32.S" ld_v00x_vector.ld
build uni_x035 rv32imac_zicsr ilp32  '-DCH32_VECTORS="vectors_x035.inc" -DCH32_MSTATUS_INIT=0x88 -DCH32_INTSYSCR_INIT=0x3 -DCH32_CORECFGR=0x1f' "$HERE/crt0_ch32.S" ld_x035.ld
build uni_v307 rv32imafc_zicsr ilp32f '-DCH32_VECTORS="vectors_v307d8c.inc" -DCH32_MSTATUS_INIT=0x6088 -DCH32_INTSYSCR_INIT=0x0b -DCH32_CORECFGR=0x1f' "$HERE/crt0_ch32.S" ld_v307.ld

fail=0
python3 "$HERE/compare.py" "$CH32_GCC_BIN" vectors_v00x.inc     evt_v00x.elf uni_v00x.elf .init   || fail=1
python3 "$HERE/compare.py" "$CH32_GCC_BIN" vectors_x035.inc     evt_x035.elf uni_x035.elf .vector || fail=1
python3 "$HERE/compare.py" "$CH32_GCC_BIN" vectors_v307d8c.inc  evt_v307.elf uni_v307.elf .vector || fail=1
"$CH32_GCC_BIN/riscv-none-elf-size" ./*.elf
exit $fail
