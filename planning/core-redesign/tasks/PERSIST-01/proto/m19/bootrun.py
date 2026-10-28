# bootrun.py CH32RV PROBE ELF STORE SIZE BANKS: flash, then 3 x (reset, wait, one read of the store); print interval and gen of each bank.
import subprocess, sys, time, os
ch, probe, elf, store, size, banks = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4], 16), int(sys.argv[5], 16), int(sys.argv[6])
c = ['--probe', probe, '--non-interactive', '--progress', 'none']
r = subprocess.run([ch, *c, 'flash', elf, '--format', 'elf', '--reset', 'run', '--confirm-run'], capture_output=True, text=True, timeout=300)
if r.returncode: sys.exit('flash failed: ' + (r.stderr or r.stdout)[-200:])
out = os.path.join(os.path.dirname(elf), 'store.bin')
unit = size // banks
for i in range(3):
    subprocess.run([ch, *c, 'reset'], capture_output=True, timeout=60); time.sleep(1.0)
    subprocess.run([ch, *c, 'read', '--range', '0x%x+0x%x' % (0x08000000 | store, size), '--format', 'bin', '-o', out], capture_output=True, timeout=120)
    b = open(out, 'rb').read(); res = []
    for k in range(banks):
        p = b[k * unit:k * unit + 16]
        if banks == 1: res.append('ver %d interval %d' % (int.from_bytes(p[0:2], 'little'), int.from_bytes(p[4:6], 'little')))
        else: res.append('bank%d gen %d interval %d' % (k, int.from_bytes(p[2:4], 'little'), int.from_bytes(p[8:10], 'little')))
    print('  reset %d: %s' % (i + 1, ' | '.join(res)))
