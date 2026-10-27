#!/bin/bash
# ld.sh [-f FQBN] TAG TEMPLATE [DEFINE=VAL ...] : build t/TEMPLATE.ino with the defines (and lf.h, st.h) using the
# experiment core copy; print code (flash) and static RAM, and the difference from t/base.ino on the same board.
X=$(dirname $(readlink -f $0)); R=/home/mt/dev_wch/ArduinoCore-CH32RV
P=/var/tmp/claude-tmp/claude-1000/-home-mt-dev-wch-ArduinoCore-CH32RV/304fc7e3-f69d-4a99-9b8d-7edb631ee0f8/scratchpad/persist
export PATH=$R/.tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin:$PATH ARDUINO_DIRECTORIES_DATA=/home/mt/.cache/ch32rv-core-bench/arduino/data ARDUINO_DIRECTORIES_DOWNLOADS=/home/mt/.cache/ch32rv-core-bench/arduino/downloads ARDUINO_DIRECTORIES_USER=$P/user
fq=CH32V003:pnum=CH32V003F4P6; [ "$1" = -f ] && { fq=$2; shift 2; }
B=$X/b/$(echo $fq | tr ':,=' '___'); tag=$1 t=$2; shift 2
sz() {  # sz NAME TEMPLATE DEFINES... -> "code ram"
  local n=$1 tp=$2; shift 2; local s=$B/sk/$n; mkdir -p $s
  { for d in "$@"; do echo "#define ${d%%=*} ${d#*=}"; done; cat $X/t/$tp.ino; } > $s/$n.ino
  cp $X/lf.h $X/st.h $s/
  if ! arduino-cli compile -b ch32-riscv-ug:ch32rv:$fq --build-path $B/o/$n $s >$B/o-$n.log 2>&1; then echo "FAIL $(grep -m2 -iE 'error' $B/o-$n.log | tr '\n' ' ')"; return; fi
  python3 - $B/o/$n/$n.ino.elf <<'PY'
import subprocess,sys
fl=ram=0
for l in subprocess.run(['riscv-none-elf-objdump','-h',sys.argv[1]],capture_output=True,text=True).stdout.split('\n'):
  f=l.split()
  if len(f)>=7 and f[0].isdigit():
    name,size,vma=f[1],int(f[2],16),int(f[3],16)
    if name in('.ch32rv_store','.stack') or not size or name.startswith('.debug') or name in ('.comment','.riscv.attributes'): continue
    if vma>=0x20000000: ram+=size
    if vma<0x20000000 or name=='.data': fl+=size if vma<0x20000000 or name=='.data' else 0
print(fl,ram)
PY
}
mkdir -p $B/o
[ -f $B/base.sz ] || sz base base > $B/base.sz
read b0 r0 < $B/base.sz
read c r <<<"$(sz $tag $t "$@")"
[ "$c" = FAIL ] && { echo "$tag FAIL $r"; exit 1; }
printf "%-24s code %5d (+%d)  ram %4d (+%d)\n" "$tag" $c $((c-b0)) $r $((r-r0))
