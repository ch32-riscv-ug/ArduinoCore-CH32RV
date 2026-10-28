# run.py CH32RV PROBE TTY ELF LEVEL : flash, send g, compare each case's line with expected.txt; depths and times.
import serial, subprocess, sys, time, os
ch, probe, tty, elf, level = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5])
c = ['--probe', probe, '--non-interactive', '--progress', 'none']
if subprocess.run([ch, *c, 'flash', elf, '--format', 'elf', '--reset', 'run', '--confirm-run'], capture_output=True, timeout=300).returncode: sys.exit('flash failed')
exp = [l.rstrip('\n').split('\t', 1) for l in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'own', 'expected.txt'))]
s = serial.Serial(tty, 115200, timeout=0.2); time.sleep(0.6); s.reset_input_buffer(); s.write(b'g'); b = b''; t = time.time()
while time.time() - t < 10 and b'END' not in b: b += s.read(4096)
prev, out, bad = None, [], []
for l in b.decode(errors='replace').split('\n'):
    l = l.rstrip('\r')
    if l.startswith('C none'): out.append('none ' + l.split()[2]); prev = None; continue
    if l.startswith('C '):
        i, d = int(l.split()[1]), l.split()[2]; lv, e = exp[i]
        st = 'ok' if prev == e else ('MISMATCH' if int(lv) <= level else 'unsupported')
        if st != 'ok': bad.append('%d %s: got %r want %r' % (i, st, prev, e))
        out.append('%d:%s' % (i, d)); prev = None; continue
    if l.startswith('T '): out.append('t20 int %s us, f %s us' % tuple(l.split()[1:3])); continue
    if l == 'END': break
    prev = l if prev is None else prev + '\\n' + l
print('  depth ' + ' '.join(out))
for x in bad: print('  ' + x)
