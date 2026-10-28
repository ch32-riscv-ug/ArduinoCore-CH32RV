# line.py CH32RV PROBE ELF TTY : flash, reset, print the last "hc" line (or what came).
import serial, subprocess, sys, time
ch, probe, elf, tty = sys.argv[1:5]
c = ['--probe', probe, '--non-interactive', '--progress', 'none']
r = subprocess.run([ch, *c, 'flash', elf, '--format', 'elf', '--reset', 'run', '--confirm-run'], capture_output=True, text=True, timeout=300)
if r.returncode: sys.exit('flash failed: ' + (r.stderr or r.stdout)[-200:])
s = serial.Serial(tty, 115200, timeout=0.2); s.reset_input_buffer(); b = b''; t = time.time()
while time.time() - t < 3: b += s.read(256)
l = [x.strip() for x in b.decode(errors='replace').split('\n') if x.startswith('hc')]
print(l[-1] if l else 'nothing: %r' % b[-60:])
