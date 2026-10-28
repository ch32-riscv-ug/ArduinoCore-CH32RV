# pf.py TTY : send g, collect until END, report each printf line's length and the marker after it.
import serial, sys, time
s = serial.Serial(sys.argv[1], 115200, timeout=0.2); time.sleep(0.5); s.reset_input_buffer(); s.write(b'g'); b = b''; t = time.time()
while time.time() - t < 8 and b'END' not in b: b += s.read(4096)
for l in b.decode(errors='replace').split('\n'):
    l = l.rstrip('\r')
    if l.startswith('L'): print('  line L%s: %d chars arrived' % (l[1:4], len(l)))
    elif l.startswith('M ') or l == 'END' or l.startswith('P'): print('  ' + l)
