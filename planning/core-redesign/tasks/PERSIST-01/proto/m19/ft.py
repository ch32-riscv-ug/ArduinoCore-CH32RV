# ft.py CH32RV PROBE ELF [TTY] : flash ELF, reset, then read the "ft" line from TTY, or (no TTY) read ft_result[]
# once with the debugger (one read per reset: V205). Take bench.lock first.
import serial, subprocess, sys, time
ch, probe, elf = sys.argv[1:4]
tty = sys.argv[4] if len(sys.argv) > 4 else None
c = ['--probe', probe, '--non-interactive', '--progress', 'none']
r = subprocess.run([ch, *c, 'flash', elf, '--format', 'elf', '--reset', 'run', '--confirm-run'], capture_output=True, text=True, timeout=180)
if r.returncode: sys.exit('flash failed: ' + (r.stderr or r.stdout)[-300:])
if tty:
    s = serial.Serial(tty, 115200, timeout=0.1); s.reset_input_buffer()
subprocess.run([ch, *c, 'reset'], capture_output=True, timeout=60)
if tty:
    b = b''; t0 = time.time()
    while time.time() - t0 < 6 and not any(l.startswith(b'ft ') for l in b.split(b'\n')[:-1]): b += s.read(64)
    print(next((l.decode().strip() for l in b.split(b'\n')[:-1] if l.startswith(b'ft ')), 'timeout %r' % b[-80:]))
else:
    time.sleep(1.5)
    nm = subprocess.run(['riscv-none-elf-nm', elf], capture_output=True, text=True).stdout
    a = next(int(l.split()[0], 16) for l in nm.splitlines() if l.endswith(' ft_result'))
    import tempfile, os
    f = tempfile.NamedTemporaryFile(delete=False); f.close()  # "-o -" prints a hex dump whatever --format says
    subprocess.run([ch, *c, 'read', '--range', '0x%x+16' % a, '--format', 'bin', '-o', f.name], capture_output=True, timeout=60)
    o = open(f.name, 'rb').read(); os.unlink(f.name)
    w = [int.from_bytes(o[i:i + 4], 'little') for i in range(0, 16, 4)]
    print('ft (debugger) magic %s steps %d fails 0x%X first 0x%X' % ('ok' if w[0] == 0xF7F7F7F7 else hex(w[0]), w[3], w[1], w[2]))
