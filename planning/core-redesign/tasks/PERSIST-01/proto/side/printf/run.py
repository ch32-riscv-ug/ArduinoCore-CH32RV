# run.py CH32RV TTY ELF : flash (by the board-identify port), send g, compare each case's line, print depths and time.
import serial, subprocess, sys, time
ch, tty, elf = sys.argv[1:4]
c = ['--probe', 'port:' + tty, '--non-interactive', '--progress', 'none']
r = subprocess.run([ch, *c, 'flash', elf, '--format', 'elf', '--reset', 'run', '--confirm-run'], capture_output=True, text=True, timeout=300)
if r.returncode: sys.exit('flash failed')
LONG = ("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ab")
EXP = ["-42|7|3000000000", "   42|42   |-0042", "beef|BEEF|1234abcd", "-123456789|4000000000",
       "A|hey|     right|l   |", "%|abc|", LONG, "3.14"]
s = serial.Serial(tty, 115200, timeout=0.2); time.sleep(0.6); s.reset_input_buffer(); s.write(b'g'); b = b''; t = time.time()
while time.time() - t < 10 and b'END' not in b: b += s.read(4096)
lines = [l.rstrip('\r') for l in b.decode(errors='replace').split('\n')]
out, prev = [], None
for l in lines:
    if l.startswith('C none'): out.append('none %s' % l.split()[2]); prev = None; continue
    if l.startswith('C '):
        i, d = int(l.split()[1]), l.split()[2]
        ok = prev == EXP[i]
        out.append('%d %s %s' % (i, d, 'ok' if ok else 'got %r' % (prev[:40] if prev is not None else None)))
        prev = None; continue
    if l.startswith('T '): out.append('time20 ' + l.split()[1]); continue
    if l == 'END': break
    prev = l if prev is None else prev + '\n' + l
print(' ; '.join(out))
