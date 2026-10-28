# hw.py CH32RV ID ELF [CHIP] : flash ELF (default erase), then over the board's UART: reset, read the boot line, set
# 250, 250 again (unchanged), 321, reset, read the boot line. Prints each line. Take bench.lock first.
import serial, subprocess, sys, time
ch, bid, elf = sys.argv[1:4]
chip = ['--chip', sys.argv[4]] if len(sys.argv) > 4 else []
c = ['--probe', 'port:/run/board-identify/by-id/' + bid, *chip, '--non-interactive', '--progress', 'none']
r = subprocess.run([ch, *c, 'flash', elf, '--format', 'elf', '--reset', 'run', '--confirm-run'], capture_output=True, text=True, timeout=120)
if r.returncode: sys.exit('flash failed: ' + (r.stderr or r.stdout)[-300:])
s = serial.Serial('/run/board-identify/by-id/' + bid, 115200, timeout=0.1)
def line(want, t=4):
    b = b''; t0 = time.time()
    while time.time() - t0 < t:
        b += s.read(64)
        for l in b.split(b'\n')[:-1]:  # complete lines only
            if want in l:
                return l.decode(errors='replace').strip()
    return 'timeout: %r' % b[-80:]
def reset():
    s.reset_input_buffer(); subprocess.run([ch, *c, 'reset'], capture_output=True, timeout=60); return line(b'interval')
print('boot  :', reset())
for v in (250, 250, 321):
    s.reset_input_buffer(); s.write(b'%d\n' % v); print('send %d:' % v, line(b'saved'))
print('reset :', reset())
