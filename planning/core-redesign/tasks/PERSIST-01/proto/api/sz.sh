#!/bin/bash
# sz.sh FQBN : code and static RAM of t/raw and of each sample's store part (the sample from its #include up to
# "Settings settings;", driven by t/drv.ino), as differences from t/base.ino; built with the experiment core copy.
X=$(dirname $(readlink -f $0)); R=/home/mt/dev_wch/ArduinoCore-CH32RV
P=/var/tmp/claude-tmp/claude-1000/-home-mt-dev-wch-ArduinoCore-CH32RV/304fc7e3-f69d-4a99-9b8d-7edb631ee0f8/scratchpad/persist
export PATH=$R/.tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin:$PATH ARDUINO_DIRECTORIES_DATA=/home/mt/.cache/ch32rv-core-bench/arduino/data ARDUINO_DIRECTORIES_DOWNLOADS=/home/mt/.cache/ch32rv-core-bench/arduino/downloads ARDUINO_DIRECTORIES_USER=$P/user
fq=$1; B=$X/b/$(echo $fq | tr ':,=' '___'); mkdir -p $B
sz() {  # sz NAME (sketch in $B/sk/NAME) -> "code ram"
  if ! arduino-cli compile -b ch32-riscv-ug:ch32rv:$fq --build-path $B/o/$1 $B/sk/$1 >$B/$1.log 2>&1; then echo "FAIL $(grep -m2 -iE 'error' $B/$1.log | tr '\n' ' ')"; return; fi
  python3 - $B/o/$1/$1.ino.elf <<'PY2'
import subprocess,sys
fl=ram=0
for l in subprocess.run(['riscv-none-elf-size','-A',sys.argv[1]],capture_output=True,text=True).stdout.split('\n'):
  f=l.split()
  if len(f)==3 and f[0].startswith('.') and f[1].isdigit():
    n,sz,a=f[0],int(f[1]),int(f[2])
    if n in('.ch32rv_store','.stack') or not sz or n.startswith('.debug') or n in('.comment','.riscv.attributes'): continue
    if a>=0x20000000: ram+=sz
    if a<0x20000000 or n=='.data': fl+=sz
print(fl,ram)
PY2
}
mk() { mkdir -p $B/sk/$1; cat > $B/sk/$1/$1.ino; }
mk base < $X/t/base.ino; mk raw < $X/t/raw.ino
for s in FlashSettings FlashSettingsTwoBanks; do
  { sed -n '/^#include <ch32rv_flash.h>/,/^Settings settings;/p' $X/$s/$s.ino | sed '$d'; cat $X/t/drv.ino; } | mk $s
done
read b0 r0 <<<"$(sz base)"
for n in raw FlashSettings FlashSettingsTwoBanks; do
  read c r <<<"$(sz $n)"; [ "$c" = FAIL ] && { echo "$n $c $r"; continue; }
  st=$(riscv-none-elf-objdump -h $B/o/$n/$n.ino.elf | awk '$2==".ch32rv_store"{print "0x"$3"@0x"$4}')
  printf "%-22s code %5d (+%d)  ram %4d (+%d)  store %s\n" $n $c $((c-b0)) $r $((r-r0)) "$st"
done
