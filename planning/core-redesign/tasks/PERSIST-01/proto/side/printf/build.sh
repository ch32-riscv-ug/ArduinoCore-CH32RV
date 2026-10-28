#!/bin/bash
# build.sh FQBN CAND MODE(size|test) : build pfm with the repository core; print code and static RAM.
R=/home/mt/dev_wch/ArduinoCore-CH32RV; H=/var/tmp/claude-tmp/claude-1000/-home-mt-dev-wch-ArduinoCore-CH32RV/304fc7e3-f69d-4a99-9b8d-7edb631ee0f8/scratchpad
export PATH=$R/.tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin:$PATH ARDUINO_DIRECTORIES_DATA=/home/mt/.cache/ch32rv-core-bench/arduino/data ARDUINO_DIRECTORIES_DOWNLOADS=/home/mt/.cache/ch32rv-core-bench/arduino/downloads ARDUINO_DIRECTORIES_USER=$H/real/user
fq=$1 c=$2 m=$3; F=${fq%%:*}; d=$H/pfm/b/$F/$c-$m; mkdir -p $d/sk/pfm
cp $H/pfm/pfm/pfm.ino $d/sk/pfm/
fl="-DCAND=$c"; [ $m = size ] && fl="$fl -DSIZE_ONLY"
if [ $c = 5 ] || [ $c = 6 ]; then cp $H/pfm/printf.c $H/pfm/printf.h $d/sk/pfm/; fi
[ $c = 5 ] && fl="$fl -DPRINTF_SUPPORT_DECIMAL_SPECIFIERS=0 -DPRINTF_SUPPORT_EXPONENTIAL_SPECIFIERS=0 -DPRINTF_SUPPORT_LONG_LONG=0 -DPRINTF_SUPPORT_WRITEBACK_SPECIFIER=0"
if ! arduino-cli compile -b ch32-riscv-ug:ch32rv:$fq --build-property "compiler.c.extra_flags=$fl" --build-property "compiler.cpp.extra_flags=$fl" --build-path $d/o $d/sk/pfm >$d.log 2>&1; then echo "$F $c $m FAIL $(grep -m1 -E 'error|overflow' $d.log)"; exit; fi
riscv-none-elf-size $d/o/pfm.ino.elf | awk -v t="$F $c $m" 'NR==2{print t, "code", $1+$2, "ram", $2+$3}'
