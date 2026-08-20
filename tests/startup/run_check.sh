#!/usr/bin/env bash
# W-2: build EVT-startup and unified-startup ELFs per family and verify equivalence.
# Requires: CH32_MIRROR_ROOT (EVT mirrors). The toolchain comes from <repo>/.tools.
# Excluded families: CH32H417 (loadcode boot) - see README.
set -euo pipefail

# Tool locations default to <repo>/.tools (tools/index/fetch_tools.py puts
# them there); anything already exported wins.
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../../tools/index/toolenv.sh"

: "${CH32_GCC_BIN:?no toolchain: run  uv run tools/index/fetch_tools.py}"
# The EVT mirrors are large clones of other repositories and are not fetched
# into .tools; point at wherever they are checked out.
: "${CH32_MIRROR_ROOT:?set CH32_MIRROR_ROOT to the directory containing the CH32* EVT mirrors}"
WORK="${1:?usage: run_check.sh <workdir>}"
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
# crt0 の正本は cores/arduino/crt0_ch32.S(このharnessはコピーを持たない)
CRT0="$REPO/cores/arduino/crt0_ch32.S"
GCC="$CH32_GCC_BIN/riscv-none-elf-gcc"

mkdir -p "$WORK"
cd "$WORK"

cat > main.c <<'EOF'
#include <stdint.h>
void SystemInit(void) {}
volatile uint32_t counter;
void SysTick_Handler(void) { counter = 1; }
int main(void) { for (;;) counter++; }
EOF

# tag|march|mabi|startup(.S relative to mirror root)|ld(relative)|unified -D defines
CONFIG=(
"v003|rv32ec_zicsr|ilp32e|CH32V003/EVT/EXAM/SRC/Startup/startup_ch32v00x.S|CH32V003/EVT/EXAM/SRC/Ld/Link.ld|-DCH32_MSTATUS_INIT=0x1880 -DCH32_INTSYSCR_INIT=0x3 -DCH32_HIGHCODE"
"v00x|rv32emc_zicsr|ilp32e|CH32V006/EVT/EXAM/SRC/Startup/startup_ch32v00X.S|CH32V006/EVT/EXAM/SRC/Ld/Link.ld|-DCH32_MSTATUS_INIT=0x1880 -DCH32_INTSYSCR_INIT=0x3"
"v20x_d6|rv32imac_zicsr|ilp32|CH32V20x/EVT/EXAM/SRC/Startup/startup_ch32v20x_D6.S|CH32V20x/EVT/EXAM/SRC/Ld/Link.ld|-DCH32_MSTATUS_INIT=0x88 -DCH32_INTSYSCR_INIT=0x3 -DCH32_CORECFGR=0x1f"
"v20x_d8|rv32imac_zicsr|ilp32|CH32V20x/EVT/EXAM/SRC/Startup/startup_ch32v20x_D8.S|CH32V20x/EVT/EXAM/SRC/Ld/Link.ld|-DCH32_MSTATUS_INIT=0x88 -DCH32_INTSYSCR_INIT=0x3 -DCH32_CORECFGR=0x1f"
"v20x_d8w|rv32imac_zicsr|ilp32|CH32V20x/EVT/EXAM/SRC/Startup/startup_ch32v20x_D8W.S|CH32V20x/EVT/EXAM/SRC/Ld/Link.ld|-DCH32_MSTATUS_INIT=0x88 -DCH32_INTSYSCR_INIT=0x3 -DCH32_CORECFGR=0x1f"
"v205|rv32imc_zicsr|ilp32|CH32V205/EVT/EXAM/SRC/Startup/startup_ch32v205.S|CH32V205/EVT/EXAM/SRC/Ld/Link.ld|-DCH32_MSTATUS_INIT=0x88 -DCH32_INTSYSCR_INIT=0x7 -DCH32_CORECFGR=0x21 -DCH32_CSR_BC1=0x1"
"m030|rv32imc_zicsr|ilp32|CH32M030/EVT/EXAM/SRC/Startup/startup_ch32m030.S|CH32M030/EVT/EXAM/SRC/Ld/Link.ld|-DCH32_MSTATUS_INIT=0x88 -DCH32_INTSYSCR_INIT=0x3 -DCH32_CORECFGR=0x21 -DCH32_CSR_BC1=0x1"
"v307_d8|rv32imafc_zicsr|ilp32f|CH32V307/EVT/EXAM/SRC/Startup/startup_ch32v30x_D8.S|CH32V307/EVT/EXAM/SRC/Ld/Link.ld|-DCH32_MSTATUS_INIT=0x6088 -DCH32_INTSYSCR_INIT=0x0b -DCH32_CORECFGR=0x1f"
"v307_d8c|rv32imafc_zicsr|ilp32f|CH32V307/EVT/EXAM/SRC/Startup/startup_ch32v30x_D8C.S|CH32V307/EVT/EXAM/SRC/Ld/Link.ld|-DCH32_MSTATUS_INIT=0x6088 -DCH32_INTSYSCR_INIT=0x0b -DCH32_CORECFGR=0x1f"
"v4x7|rv32imac_zicsr|ilp32|CH32V407/EVT/EXAM/SRC/Startup/startup_ch32v4x7.S|CH32V407/EVT/EXAM/SRC/Ld/Link.ld|-DCH32_MSTATUS_INIT=0x688 -DCH32_INTSYSCR_INIT=0x07 -DCH32_CORECFGR=0x21 -DCH32_CSR_BC1=0x01 -DCH32_CSR805_CLR=0x100"
"x035|rv32imac_zicsr|ilp32|CH32X035/EVT/EXAM/SRC/Startup/startup_ch32x035.S|CH32X035/EVT/EXAM/SRC/Ld/Link.ld|-DCH32_MSTATUS_INIT=0x88 -DCH32_INTSYSCR_INIT=0x3 -DCH32_CORECFGR=0x1f"
"x3x5|rv32imafc_zicsr|ilp32f|CH32X315/EVT/EXAM/SRC/Startup/startup_ch32x3x5.S|CH32X315/EVT/EXAM/SRC/Ld/Link.ld|-DCH32_MSTATUS_INIT=0x6088 -DCH32_INTSYSCR_INIT=0x07 -DCH32_CORECFGR=0x123703E1 -DCH32_CSR_BC1=0x01"
"v103|rv32imac_zicsr|ilp32|CH32V103/EVT/EXAM/SRC/Startup/startup_ch32v10x.S|CH32V103/EVT/EXAM/SRC/Ld/Link.ld|-DCH32_MSTATUS_INIT=0x88 -DCH32_MTVEC_MODE=1"
"l103|rv32imac_zicsr|ilp32|CH32L103/EVT/EXAM/SRC/Startup/startup_ch32l103.S|CH32L103/EVT/EXAM/SRC/Ld/Link.ld|-DCH32_MSTATUS_INIT=0x88 -DCH32_INTSYSCR_INIT=0x3 -DCH32_CORECFGR=0x1f"
)

fail=0
for row in "${CONFIG[@]}"; do
  IFS='|' read -r tag march mabi startup ld defines <<<"$row"
  startup="$CH32_MIRROR_ROOT/$startup"
  ld_src="$CH32_MIRROR_ROOT/$ld"
  echo "===== $tag ($march/$mabi)"

  # Vector spec generated from the local EVT mirror; MUST NOT be committed.
  uv run --no-project python "$HERE/extract_vectors.py" "$startup" "vectors_$tag.inc"

  # EVT table location: .vector if the startup defines _vector_base, else .init.
  if grep -q "_vector_base" "$startup"; then evt_section=".vector"; else evt_section=".init"; fi

  # Unified builds need a .vector output section; add one when the EVT ld lacks it.
  cp "$ld_src" "ld_$tag.ld"
  if grep -q '\.vector' "ld_$tag.ld"; then
    cp "ld_$tag.ld" "ld_${tag}_uni.ld"
  else
    uv run --no-project python - "ld_$tag.ld" "ld_${tag}_uni.ld" <<'EOF'
import sys
src = open(sys.argv[1]).read()
open(sys.argv[2], 'w').write(src.replace(
    '} >FLASH AT>FLASH',
    '} >FLASH AT>FLASH\n\t.vector : { *(.vector); . = ALIGN(64); } >FLASH AT>FLASH', 1))
EOF
  fi

  $GCC -march="$march" -mabi="$mabi" -Os -c main.c -o "main_$tag.o"
  $GCC -march="$march" -mabi="$mabi" -Os -c "$startup" -o "startup_evt_$tag.o"
  $GCC -march="$march" -mabi="$mabi" -nostartfiles -T "ld_$tag.ld" -Wl,--gc-sections \
       "main_$tag.o" "startup_evt_$tag.o" -o "evt_$tag.elf"
  # shellcheck disable=SC2086
  $GCC -march="$march" -mabi="$mabi" -Os -I. -DCH32_VECTORS=vectors_$tag.inc $defines \
       -c "$CRT0" -o "startup_uni_$tag.o"
  $GCC -march="$march" -mabi="$mabi" -nostartfiles -T "ld_${tag}_uni.ld" -Wl,--gc-sections \
       "main_$tag.o" "startup_uni_$tag.o" -o "uni_$tag.elf"

  uv run --no-project python "$HERE/compare.py" "$CH32_GCC_BIN" "vectors_$tag.inc" "evt_$tag.elf" "uni_$tag.elf" "$evt_section" || fail=1
done

"$CH32_GCC_BIN/riscv-none-elf-size" evt_*.elf uni_*.elf
exit $fail
