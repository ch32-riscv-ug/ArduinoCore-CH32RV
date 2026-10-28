import serial, subprocess, sys, time
ch, tty, elf = sys.argv[1:4]
c = ['--probe', 'port:' + tty, '--non-interactive', '--progress', 'none']
if subprocess.run([ch, *c, 'flash', elf, '--format', 'elf', '--reset', 'run', '--confirm-run'], capture_output=True, timeout=300).returncode: sys.exit('flash failed')
s = serial.Serial(tty, 115200, timeout=0.2); time.sleep(0.6); s.reset_input_buffer(); s.write(b'g'); b = b''; t = time.time()
while time.time() - t < 6 and b'END' not in b: b += s.read(4096)
print(b.decode(errors='replace').replace('\r', '').split('END')[0].strip())
