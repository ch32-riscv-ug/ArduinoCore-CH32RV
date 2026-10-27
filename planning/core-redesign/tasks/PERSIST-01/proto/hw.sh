#!/bin/bash
# hw.sh: on the bench V003 F4P6, check the experiment EEPROM across power cycles and re-uploads.
P=$(dirname $(readlink -f $0)); R=/home/mt/dev_wch/ArduinoCore-CH32RV; CH=$R/.tools/ch32rv/0.23.2/ch32rv
PR=port:/run/board-identify/by-id/ch32v003f4p6-f9e1abcd6201bc53; TTY=/run/board-identify/by-id/ch32v003f4p6-f9e1abcd6201bc53
D=/sys/bus/usb/devices/3-2.1:1.0/3-2.1-port1/disable; E=$P/b/CH32V003_pnum_CH32V003F4P6-eedef/eedef.ino.elf
C=(--probe $PR --chip CH32V003F4P6 --non-interactive --progress none)
ser() { sleep 2.5; $R/tests/.venv/bin/python - $TTY <<'PY'
import serial,sys,time
s=serial.Serial(sys.argv[1],115200,timeout=0.2); t=time.time(); b=b''
while time.time()-t<2.5: b+=s.read(256)
l=[x for x in b.decode(errors='replace').split('\n') if x.startswith('boots')]; print(l[-1].strip() if l else 'no output')
PY
}
cyc() { echo 1 > $D; sleep 1; echo 0 > $D; sleep 2; echo "power cycle: $(ser)"; }
up() { timeout 120 $CH "${C[@]}" flash $E --format elf --reset run --confirm-run "$@" >/dev/null 2>&1 || echo "flash failed"; echo "upload $*: $(ser)"; }
up
cyc; cyc; cyc
timeout 30 $CH "${C[@]}" read --range 0x08003F80+128 --format hex-dump -o - 2>/dev/null | head -8
echo "after the read: $(ser)"
up
cyc
up --erase sector
cyc
