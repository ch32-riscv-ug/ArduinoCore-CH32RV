#!/bin/bash
# mo.sh TAG FQBN "BEGIN" "COMMIT" : same as m.sh, with this repository's core (experiment copy with the store section).
X=$(dirname $(readlink -f $0)); P=$X/../persist; R=/home/mt/dev_wch/ArduinoCore-CH32RV; tag=$1 fq=$2 bg=$3 cm=$4
export PATH=$R/.tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin:$PATH ARDUINO_DIRECTORIES_DATA=/home/mt/.cache/ch32rv-core-bench/arduino/data ARDUINO_DIRECTORIES_DOWNLOADS=/home/mt/.cache/ch32rv-core-bench/arduino/downloads ARDUINO_DIRECTORIES_USER=$P/user
for n in base rd rw put; do
  mkdir -p $X/skv/$tag-$n
  { echo "#define EEPROM_BEGIN $bg"; echo "#define EEPROM_COMMIT $cm"; cat $X/sk/$n/$n.ino; } > $X/skv/$tag-$n/$tag-$n.ino
  o=$X/b/$tag-$n
  if ! arduino-cli compile -b ch32-riscv-ug:ch32rv:$fq --build-path $o $X/skv/$tag-$n >$o.log 2>&1; then echo "$tag $n FAILED: $(grep -m2 -i error $o.log)"; continue; fi
  e=$o/$tag-$n.ino.elf
  python3 - $e $tag $n <<'PY'
import subprocess,sys
e,t,n=sys.argv[1:4]; fl=ram=0
for l in subprocess.run(['riscv-none-elf-objdump','-h',e],capture_output=True,text=True).stdout.split('\n'):
  f=l.split()
  if len(f)>=7 and f[0].isdigit():
    name,size,vma=f[1],int(f[2],16),int(f[3],16)
    if name in('.ch32rv_store','.stack') or not size: continue
    if 'ALLOC' not in l and True: pass
    if vma>=0x20000000: ram+=size
    elif name in('.vector','.init','.text','.preinit_array','.init_array','.fini_array','.dlalign','.highcode','.data'): fl+=size
print(t,n,fl,ram)
PY
done | awk '{if($2=="base"){f0=$3;r0=$4} printf "%-10s %-5s flash %6d (+%d)  ram %5d (+%d)\n",$1,$2,$3,$3-f0,$4,$4-r0}'
