#!/usr/bin/env bash
# R-09: newlib(-nano) size measurement on the unified crt0 + own linker script.
# Sizes only - nothing is executed. Feeds Q-022/Q-051 and the toolchain matrix.
# Requires: CH32_GCC_BIN (riscv-none-elf-gcc bin dir)
set -euo pipefail

: "${CH32_GCC_BIN:?set CH32_GCC_BIN to the xPack riscv-none-elf-gcc bin directory}"
WORK="${1:?usage: run_sizebench.sh <workdir>}"
HERE="$(cd "$(dirname "$0")" && pwd)"
PLATFORM="$HERE/../platform/ch32v"
GCC="$CH32_GCC_BIN/riscv-none-elf-gcc"
GXX="$CH32_GCC_BIN/riscv-none-elf-g++"

mkdir -p "$WORK"
RESULTS="$WORK/results.md"

cat > "$WORK/sizebench.ld" <<'LDEOF'
ENTRY( _start )
MEMORY
{
    FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 1M
    RAM (xrw)  : ORIGIN = 0x20000000, LENGTH = 128K
}
INCLUDE sections.ld
LDEOF

CRT_DEFS=(-DCH32_VECTORS=vectors_ch32v00x.inc -DCH32_MSTATUS_INIT=0x1880 -DCH32_INTSYSCR_INIT=0x3)
COMMON=(-Os -g -ffunction-sections -fdata-sections)
# Measurement-only linker script with generous MEMORY so no case is clipped by
# a real SKU's flash limit (fit-per-SKU is judged from the numbers, not here).
LINK=(-nostartfiles -Wl,--gc-sections -Wl,--no-warn-rwx-segments
      "-L$PLATFORM/variants/CH32V00X" "-T$WORK/sizebench.ld")
CXXFLAGS=(-fno-exceptions -fno-rtti -fno-threadsafe-statics)

build_one() { # arch_tag march mabi case_file flavor_tag extra_link_flags...
  local arch_tag=$1 march=$2 mabi=$3 case_file=$4 flavor=$5; shift 5
  local name tag cc std_flags=()
  name=$(basename "$case_file"); name=${name%.*}
  tag="${name}_${flavor}_${arch_tag}"
  if [[ "$case_file" == *.cpp ]]; then cc=$GXX; std_flags=("${CXXFLAGS[@]}"); else cc=$GCC; fi

  $GCC -march="$march" -mabi="$mabi" "${COMMON[@]}" "-I$PLATFORM/cores/arduino" \
       "${CRT_DEFS[@]}" -c "$HERE/../startup/crt0_ch32.S" -o "$WORK/crt0_$tag.o"
  $GCC -march="$march" -mabi="$mabi" "${COMMON[@]}" -c "$HERE/syscalls.c" -o "$WORK/sys_$tag.o"
  $cc  -march="$march" -mabi="$mabi" "${COMMON[@]}" "${std_flags[@]}" -c "$case_file" -o "$WORK/case_$tag.o"
  $cc  -march="$march" -mabi="$mabi" "${LINK[@]}" "$@" \
       "$WORK/case_$tag.o" "$WORK/sys_$tag.o" "$WORK/crt0_$tag.o" -o "$WORK/$tag.elf"

  local line
  line=$("$CH32_GCC_BIN/riscv-none-elf-size" "$WORK/$tag.elf" | awk 'NR==2 {printf "%s | %s | %s", $1, $2, $3}')
  echo "| $name | $flavor | $arch_tag | $line |" >> "$RESULTS"
}

{
  echo "| case | libc | arch | text | data | bss |"
  echo "|---|---|---|---:|---:|---:|"
} > "$RESULTS"

for arch in "rv32ec:rv32emc_zicsr:ilp32e" "rv32imac:rv32imac_zicsr:ilp32"; do
  IFS=':' read -r arch_tag march mabi <<<"$arch"
  for case_file in "$HERE"/cases/*.c "$HERE"/cases/*.cpp; do
    build_one "$arch_tag" "$march" "$mabi" "$case_file" nano --specs=nano.specs
    build_one "$arch_tag" "$march" "$mabi" "$case_file" full
  done
  # nano + float printf support (the case where nano needs an explicit opt-in)
  build_one "$arch_tag" "$march" "$mabi" "$HERE/cases/12_printf_float.c" nano+f \
            --specs=nano.specs -Wl,-u,_printf_float
done

cat "$RESULTS"
