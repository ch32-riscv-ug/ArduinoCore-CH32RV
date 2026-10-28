# stall.py CH32RV PROBE ELF [TTY] : flash, reset, then the "stall" UART line or st_result[] read once by the debugger.
import serial, subprocess, sys, time, os, tempfile
ch, probe, elf = sys.argv[1:4]
tty = sys.argv[4] if len(sys.argv) > 4 else None
c = ['--probe', probe, '--non-interactive', '--progress', 'none']
r = subprocess.run([ch, *c, 'flash', elf, '--format', 'elf', '--reset', 'run', '--confirm-run'], capture_output=True, text=True, timeout=300)
if r.returncode: sys.exit('flash failed: ' + (r.stderr or r.stdout)[-200:])
if tty: s = serial.Serial(tty, 115200, timeout=0.1); s.reset_input_buffer()
subprocess.run([ch, *c, 'reset'], capture_output=True, timeout=60)
if tty:
    b = b''; t0 = time.time()
    while time.time() - t0 < 6 and not any(l.startswith(b'stall') for l in b.split(b'\n')[:-1]): b += s.read(64)
    print(next((l.decode().strip() for l in b.split(b'\n')[:-1] if l.startswith(b'stall')), 'timeout %r' % b[-60:])); sys.exit()
time.sleep(1.5)
nm = subprocess.run(['riscv-none-elf-nm', elf], capture_output=True, text=True).stdout
a = next(int(l.split()[0], 16) for l in nm.splitlines() if l.endswith(' st_result'))
f = tempfile.NamedTemporaryFile(delete=False); f.close()
subprocess.run([ch, *c, 'read', '--range', '0x%x+40' % a, '--format', 'bin', '-o', f.name], capture_output=True, timeout=60)
o = open(f.name, 'rb').read(); os.unlink(f.name)
w = [int.from_bytes(o[i:i + 4], 'little') for i in range(0, 40, 4)]
print('stall (debugger) %s us %s ms %s micros %s' % ('ok' if w[0] == 0x57A11 else hex(w[0]), w[1:4], w[4:7], w[7:10]))
