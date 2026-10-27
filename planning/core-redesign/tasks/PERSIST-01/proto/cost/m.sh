#!/bin/bash
# m.sh TAG FQBN "BEGIN" "COMMIT" [SIZE_TOOL] : build base/rd/rw/put for one board with the macros filled in, print
# flash (text+data) and static RAM (data+bss), and the deltas from base.
X=$(dirname $(readlink -f $0)); tag=$1 fq=$2 bg=$3 cm=$4
mkdir -p $X/b $X/skv
for n in base rd rw put; do
  d=$X/skv/$tag-$n/$tag-$n; mkdir -p $(dirname $d)/$tag-$n
  { echo "#define EEPROM_BEGIN $bg"; echo "#define EEPROM_COMMIT $cm"; cat $X/sk/$n/$n.ino; } > $X/skv/$tag-$n/$tag-$n.ino
  o=$X/b/$tag-$n
  if ! arduino-cli --config-file $X/cli.yaml compile -b $fq --build-path $o $X/skv/$tag-$n >$o.log 2>&1; then echo "$tag $n FAILED: $(grep -m2 -i error $o.log)"; continue; fi
  st=$(ls $X/data/packages/*/tools/*/*/bin/*-size 2>/dev/null | head -0); e=$o/$tag-$n.ino.elf
  sz=$(find $X/data/packages -name '*-size' -path "*$5*" | head -1)
  read t d b <<<$($sz $e | awk 'NR==2{print $1,$2,$3}')
  echo "$tag $n $((t+d)) $((d+b))"
done | awk '{if($2=="base"){f0=$3;r0=$4} printf "%-10s %-5s flash %6d (+%d)  ram %5d (+%d)\n",$1,$2,$3,$3-f0,$4,$4-r0}'
