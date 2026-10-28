#!/bin/bash
# m19.sh FQBN CONFIG "DEFINES" SCENARIO... : build t/SCENARIO.ino (and t/base.ino) with the experiment core copy and
# -D DEFINES on every compile (core included); CONFIG "nolto" also picks the menu without LTO. Prints one line per
# scenario: fqbn config scenario code(+delta) ram(+delta).
X=$(dirname $(readlink -f $0)); R=/home/mt/dev_wch/ArduinoCore-CH32RV
P=/var/tmp/claude-tmp/claude-1000/-home-mt-dev-wch-ArduinoCore-CH32RV/304fc7e3-f69d-4a99-9b8d-7edb631ee0f8/scratchpad/persist
export PATH=$R/.tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin:$PATH ARDUINO_DIRECTORIES_DATA=/home/mt/.cache/ch32rv-core-bench/arduino/data ARDUINO_DIRECTORIES_DOWNLOADS=/home/mt/.cache/ch32rv-core-bench/arduino/downloads ARDUINO_DIRECTORIES_USER=$P/user
fq=$1 cfg=$2 defs=$3; shift 3
fqb=$fq; [[ $cfg == *nolto* ]] && fqb="$fq$([[ $fq == *:* ]] && echo , || echo :)opt=osstd"
B=$X/b/$(echo $fq | tr ':,=' '___')/$cfg; mkdir -p $B
fl=""; for d in $defs; do fl="$fl -D$d"; done
sz() {
  mkdir -p $B/sk/$1; cp $X/t/$1.ino $B/sk/$1/$1.ino; cp $X/t/fi.h $B/sk/$1/
  if ! arduino-cli compile -b ch32-riscv-ug:ch32rv:$fqb --build-property "compiler.c.extra_flags=$fl" --build-property "compiler.cpp.extra_flags=$fl" --build-path $B/o/$1 $B/sk/$1 >$B/$1.log 2>&1; then echo "FAIL $(grep -m1 -E 'error' $B/$1.log)"; return; fi
  python3 - $B/o/$1/$1.ino.elf <<'PY'
import subprocess,sys
fl=ram=0
for l in subprocess.run(['riscv-none-elf-size','-A',sys.argv[1]],capture_output=True,text=True).stdout.split('\n'):
  f=l.split()
  if len(f)==3 and f[0].startswith('.') and f[1].isdigit():
    n,s,a=f[0],int(f[1]),int(f[2])
    if n in('.ch32rv_store','.stack') or not s or n.startswith('.debug') or n in('.comment','.riscv.attributes'): continue
    if a>=0x20000000 and n!='.image': ram+=s
    if a<0x20000000 or n in('.data','.image'): fl+=s
print(fl,ram)
PY
}
read b0 r0 <<<"$(sz base)"
for n in "$@"; do read c r <<<"$(sz $n)"; [ "$c" = FAIL ] && { echo "$fq $cfg $n $c $r"; continue; }
  printf "%-28s %-8s %-5s %6d %+5d  ram %+d\n" $fq $cfg $n $c $((c-b0)) $((r-r0)); done
