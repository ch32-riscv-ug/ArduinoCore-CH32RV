#!/bin/bash
# build.sh FQBN IMPL LEVEL MODE : build own.ino with the repository core; print code and static RAM.
R=/home/mt/dev_wch/ArduinoCore-CH32RV; H=/var/tmp/claude-tmp/claude-1000/-home-mt-dev-wch-ArduinoCore-CH32RV/304fc7e3-f69d-4a99-9b8d-7edb631ee0f8/scratchpad
export PATH=$R/.tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin:$PATH ARDUINO_DIRECTORIES_DATA=/home/mt/.cache/ch32rv-core-bench/arduino/data ARDUINO_DIRECTORIES_DOWNLOADS=/home/mt/.cache/ch32rv-core-bench/arduino/downloads ARDUINO_DIRECTORIES_USER=$H/real/user
fq=$1 i=$2 l=$3 m=$4; F=${fq%%:*}; d=$H/own/b/$F/$i-$l-$m; mkdir -p $d/sk/own; cp $H/own/own/* $d/sk/own/
fl="-DIMPL=$i -DOWN_LEVEL=$l -DMODE=$m"
if ! arduino-cli compile -b ch32-riscv-ug:ch32rv:$fq --build-property "compiler.cpp.extra_flags=$fl" --build-path $d/o $d/sk/own >$d.log 2>&1; then echo "$F impl$i lv$l mode$m FAIL $(grep -m1 -E 'error|overflow' $d.log)"; exit; fi
riscv-none-elf-size $d/o/own.ino.elf | awk -v t="$F impl$i lv$l mode$m" 'NR==2{print t, "code", $1+$2, "ram", $2+$3}'
