#!/bin/bash
# build.sh FQBN SKETCH... : build each sketch with the experiment core; print text+data (flash), data+bss (RAM),
# and the store section's address and size.
P=$(dirname $(readlink -f $0)); R=/home/mt/dev_wch/ArduinoCore-CH32RV
export PATH=$R/.tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin:$PATH ARDUINO_DIRECTORIES_DATA=/home/mt/.cache/ch32rv-core-bench/arduino/data ARDUINO_DIRECTORIES_DOWNLOADS=/home/mt/.cache/ch32rv-core-bench/arduino/downloads ARDUINO_DIRECTORIES_USER=$P/user
fq=$1; shift; B=$(echo $fq | tr ':,=' '___')
for n in "$@"; do
  o=$P/b/$B-$n; mkdir -p $o
  if ! arduino-cli compile -b ch32-riscv-ug:ch32rv:$fq --build-path $o $P/sk/$n >$o.log 2>&1; then
    echo "$n: BUILD FAILED: $(grep -m3 -iE 'error|overflow|will not fit|overlap' $o.log | tr '\n' ' ')"; continue; fi
  e=$o/$n.ino.elf
  read t d b <<<$(riscv-none-elf-size $e | awk 'NR==2{print $1,$2,$3}')
  st=$(riscv-none-elf-objdump -h $e | awk '$2==".ch32rv_store"{print "0x"$4, "0x"$3}')
  echo "$n: flash $((t+d)) ram $((d+b)) store ${st:-none} bin $(stat -c%s $o/$n.ino.bin)"
done
