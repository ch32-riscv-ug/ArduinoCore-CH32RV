# pc.py CH32RV LINK_SERIAL HUB PORT CYCLES ELF : power-cut test of FlashSettingsTwoBanks (pc/pc.ino). Flash, first save,
# then per cycle: "g 0" (three saves at once), wait X ms (random in DRANGE, or SWEEP), cut the LinkE's hub port with a
# ClearPortFeature(PORT_POWER) sent straight to the hub (uhubctl takes 1.6-1.9 s, +-150 ms), power back after 1.5 s,
# ask "r". Every boot must load, with a value between the one before the cycle and the last attempted.
# Run with: uv run --no-project --with pyusb --with libusb-package --with pyserial python pc.py ...
# Take bench.lock first.
import os, random, serial, subprocess, sys, time
import usb.core, libusb_package
ch, bid, hub, port, cycles, elf = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5]), sys.argv[6]
# D: random in [LO, HI] ms (default 0..120), or a fixed sweep with SWEEP=a,b,c
LO, HI = (int(x) for x in os.environ.get('DRANGE', '0,120').split(','))
SWEEP = [int(x) for x in os.environ['SWEEP'].split(',')] if os.environ.get('SWEEP') else None
be = libusb_package.get_libusb1_backend()
hb, hp = hub.split('-'); hp = tuple(int(x) for x in hp.split('.'))
HUB = next(d for d in usb.core.find(find_all=True, bDeviceClass=9, backend=be) if d.bus == int(hb) and tuple(d.port_numbers or ()) == hp)
def power(on):  # SET_FEATURE / CLEAR_FEATURE(PORT_POWER) on the hub's port
    HUB.ctrl_transfer(0x23, 3 if on else 1, 8, int(port), None)
# /dev/serial/by-id comes back right after the port does; board-identify's link takes much longer
tty = '/dev/serial/by-id/usb-wch.cn_WCH-Link_%s-if01' % bid
c = ['--probe', 'wchlink://' + bid, '--non-interactive', '--progress', 'none']
r = subprocess.run([ch, *c, 'flash', elf, '--format', 'elf', '--reset', 'run', '--confirm-run'], capture_output=True, text=True, timeout=300)
if r.returncode: sys.exit('flash failed: ' + (r.stderr or r.stdout)[-200:])

def opened(wait=30):
    t = time.time()
    while time.time() - t < wait:
        if os.path.exists(tty):
            try:
                s = serial.Serial(tty, 115200, timeout=0.1); time.sleep(0.3); s.reset_input_buffer(); return s
            except Exception:
                pass
        time.sleep(0.5)
    sys.exit('no tty')

def ask(s, cmd, want, t=3, tries=4):
    for _ in range(tries):
        s.reset_input_buffer(); s.write(cmd); b = b''; t0 = time.time()
        while time.time() - t0 < t:
            b += s.read(64)
            for l in b.split(b'\n')[:-1]:
                if l.startswith(want): return l.decode(errors='replace').strip()
    return None

s = opened()
print(ask(s, b'i\n', b'init'), flush=True)
v0 = 1000; hits = bad = 0
for n in range(cycles):
    d = SWEEP[n] if SWEEP else random.randint(LO, HI)
    s.write(b'g 0\n'); s.flush()
    t = time.perf_counter() + d / 1000
    while time.perf_counter() < t: pass
    power(False)
    try: s.close()
    except Exception: pass
    time.sleep(1.5)
    power(True)
    time.sleep(2.0)
    s = opened()
    l = ask(s, b'r\n', b'pc ')
    if l is None:
        print('%2d x=%3d no report' % (n, d), flush=True); bad += 1; continue
    f = l.split(); loaded, value = int(f[2]), int(f[4]); a, b = int(f[6]), int(f[9])
    ok = loaded == 1 and v0 <= value <= v0 + 3
    hit = not (a and b)
    hits += hit; bad += not ok
    print('%2d x=%3d %s value %d (%+d) a %d b %d%s' % (n, d, 'ok ' if ok else 'BAD', value, value - v0, a, b, '  <- cut while writing' if hit else ''), flush=True)
    if loaded: v0 = value
print('cycles %d bad %d cut-while-writing %d' % (cycles, bad, hits))
