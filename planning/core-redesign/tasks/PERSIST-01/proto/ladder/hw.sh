#!/bin/bash
# [FAM=v003|x035|v203] hw.sh [--erase MODE] NAME... : on the bench chip of FAM (default V003 F4P6), flash b/.../o/NAME and print the line the sketch sends (take bench.lock first).
X=$(dirname $(readlink -f $0)); R=/home/mt/dev_wch/ArduinoCore-CH32RV; CH=$R/.tools/ch32rv/0.23.2/ch32rv
case ${FAM:-v003} in
  v003) ID=ch32v003f4p6-f9e1abcd6201bc53 CHIP=CH32V003F4P6 B=CH32V003_pnum_CH32V003F4P6;;
  x035) ID=ch32x035c8t6-1ff9abcd880ebc48 CHIP=CH32X035C8T6 B=CH32X035_pnum_CH32X035C8T6;;
  v203) ID=ch32v203c8t6-b661abcd1e91bc63 CHIP=CH32V203C8T6 B=CH32V203_pnum_CH32V203C8T6;;
esac
PR=port:/run/board-identify/by-id/$ID TTY=/run/board-identify/by-id/$ID
C=(--probe $PR --chip $CHIP --non-interactive --progress none)
EX=(); [ "$1" = --erase ] && { EX=(--erase $2); shift 2; }
for n in "$@"; do
  E=$X/b/$B/o/$n/$n.ino.elf
  timeout 120 $CH "${C[@]}" flash $E --format elf --reset run --confirm-run "${EX[@]}" >/dev/null 2>&1 || { echo "$n: flash failed"; continue; }
  echo "$n ${EX[*]}: $($R/tests/.venv/bin/python - $TTY <<'PY'
import serial,sys,time
s=serial.Serial(sys.argv[1],115200,timeout=0.2); t=time.time(); b=b''
while time.time()-t<6 and b'millis' not in b: b+=s.read(256)
time.sleep(0.2); b+=s.read(256)
l=[x for x in b.decode(errors='replace').split('\n') if x.startswith('kind')]; print(l[-1].strip() if l else 'no output: %r'%b[-80:])
PY
)"
done
