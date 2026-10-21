#!/bin/bash
# Patch a function of a CH32H41x sketch that crashed in RAM into "return 0",
# then start the image again from ch32rv_loadcode_ram (after the flash copy),
# so the patch survives and .bss is cleared again. See docs/h417-bench.ja.md.
#
# usage: ram_restart.sh <sketch.elf> <function symbol, as nm -C prints it>
# Needs the CPU halted in M mode (in a trap handler, not in loop()).
set -e
ELF=$1; FN=$2
C=${CH32RV:-ch32rv}
P="--probe ${PROBE:-serial:49808F06CE30} --non-interactive --chip CH32H417"
NM=${NM:-riscv-none-elf-nm}
addr=$($NM -C "$ELF" | awk -v f="$FN" '{s=$3; for (i=4;i<=NF;i++) s=s" "$i} s==f {print "0x"$1; exit}')
entry=$($NM "$ELF" | awk '$3=="ch32rv_loadcode_ram"{print "0x"$1}')
sp=$($NM "$ELF" | awk '$3=="_eusrstack"{print "0x"$1}')
[ -n "$addr" ] && [ -n "$entry" ] && [ -n "$sp" ] || { echo "symbols not found"; exit 1; }
echo "patch $FN at $addr; restart at $entry, sp $sp"
$C $P dbg halt
mcause=$($C $P dbg reg read csr:834 | awk '{print $3}')
echo "mcause $mcause (the halt must be in a trap handler: M mode)"
# c.li a0,0; c.jr ra = bytes 01 45 82 80, at a 2-byte-aligned address: read
# the two words around it, splice, write both back.
base=$(printf '0x%08x' $(( addr & ~3 )))
tmp=$(mktemp)
$C $P read --range "$base+8" -o "$tmp" >/dev/null
words=$(python3 - "$tmp" $(( addr & 3 )) <<'PY'
import struct, sys
b = bytearray(open(sys.argv[1], 'rb').read())
off = int(sys.argv[2])
b[off:off + 4] = bytes([0x01, 0x45, 0x82, 0x80])
print(' '.join('0x%08x' % w for w in struct.unpack('<2I', bytes(b))))
PY
)
rm -f "$tmp"
set -- $words
$C $P --yes write --at "$base" word:$1
$C $P --yes write --at "$(printf '0x%08x' $(( base + 4 )))" word:$2
$C $P dbg reg write x2 "$sp"
$C $P dbg reg write pc "$entry"
$C $P dbg resume
