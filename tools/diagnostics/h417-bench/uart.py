"""Send one UsbFallback command over a serial port and print the reply.
usage: uart.py PORT [p|f|s]   (default p: the alive check)"""
import os, sys, time, serial
port, cmd = sys.argv[1], (sys.argv[2] if len(sys.argv) > 2 else 'p')
with serial.Serial(port, int(os.environ.get('BAUD', 115200)), timeout=0.5) as s:
    s.reset_input_buffer()
    s.write((cmd + '\n').encode())
    time.sleep(0.3)
    r = s.read(200).decode(errors='replace').strip()
    print(r or 'no reply')
    sys.exit(0 if r or cmd != 'p' else 1)
