# rb.py CH32RV ID ELF : flash, reset, print the line with "readback".
import serial, subprocess, sys, time
ch, bid, elf = sys.argv[1:4]
c = ['--probe', 'port:/run/board-identify/by-id/' + bid, '--non-interactive', '--progress', 'none']
r = subprocess.run([ch, *c, 'flash', elf, '--format', 'elf', '--reset', 'run', '--confirm-run'], capture_output=True, text=True, timeout=120)
if r.returncode: sys.exit('flash failed')
s = serial.Serial('/run/board-identify/by-id/' + bid, 115200, timeout=0.1)
s.reset_input_buffer(); subprocess.run([ch, *c, 'reset'], capture_output=True, timeout=60)
b = b''; t0 = time.time()
while time.time() - t0 < 5 and b'unaligned' not in b.split(b'\n')[-1] and not any(b'unaligned' in l for l in b.split(b'\n')[:-1]): b += s.read(64)
print([l.decode().strip() for l in b.split(b'\n') if b'readback' in l] or b[-80:])
