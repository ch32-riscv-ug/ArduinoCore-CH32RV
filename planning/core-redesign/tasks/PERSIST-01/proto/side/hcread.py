# hcread.py CH32RV PROBE ELF : flash, reset, read hc_result[] once with the debugger.
import os, subprocess, sys, tempfile, time
ch, probe, elf = sys.argv[1:4]
c = ['--probe', probe, '--non-interactive', '--progress', 'none']
r = subprocess.run([ch, *c, 'flash', elf, '--format', 'elf', '--reset', 'run', '--confirm-run'], capture_output=True, text=True, timeout=300)
if r.returncode: sys.exit('flash failed: ' + (r.stderr or r.stdout)[-200:])
subprocess.run([ch, *c, 'reset'], capture_output=True, timeout=60); time.sleep(1.5)
nm = subprocess.run(['riscv-none-elf-nm', elf], capture_output=True, text=True).stdout
a = next(int(l.split()[0], 16) for l in nm.splitlines() if l.endswith(' hc_result'))
f = tempfile.NamedTemporaryFile(delete=False); f.close()
subprocess.run([ch, *c, 'read', '--range', '0x%x+12' % a, '--format', 'bin', '-o', f.name], capture_output=True, timeout=60)
o = open(f.name, 'rb').read(); os.unlink(f.name)
w = [int.from_bytes(o[i:i + 4], 'little') for i in range(0, 12, 4)]
print('hc (debugger) %s addr 0x%X r %d' % ('ok' if w[0] == 0x4843 else hex(w[0]), w[1], w[2]))
