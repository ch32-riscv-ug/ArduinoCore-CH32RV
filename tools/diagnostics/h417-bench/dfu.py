"""Minimal DFU 1.1 download (dfu-util -D equivalent) for the bench."""
import sys, time, usb.core, usb.util, libusb_package
be = libusb_package.get_libusb1_backend()
VID, PID = 0x1209, 0x0001

def find():
    for d in usb.core.find(find_all=True, idVendor=VID, idProduct=PID, backend=be):
        for cfg in d:
            for itf in cfg:
                if itf.bInterfaceClass == 0xFE and itf.bInterfaceSubClass == 1:
                    return d, itf.bInterfaceNumber, itf.bInterfaceProtocol
    return None, None, None

def status(d, itf):
    s = bytes(d.ctrl_transfer(0xA1, 3, 0, itf, 6))
    return s[0], s[1] | s[2] << 8 | s[3] << 16, s[4]

image = open(sys.argv[1], 'rb').read()
d, itf, proto = find()
if d is None:
    # USBSS with a configuration the host read wrong: the DFU runtime is
    # interface 2 of the bench sketches; a DETACH there still works. If DFU
    # mode on USBSS never configures, UsbFallback moves it to USBFS.
    d = usb.core.find(idVendor=VID, idProduct=PID, backend=be)
    assert d is not None, 'no device'
    itf, proto = int(sys.argv[2]) if len(sys.argv) > 2 else 2, 1
print('found protocol', proto)
if proto == 1:
    try:
        d.ctrl_transfer(0x21, 0, 1000, itf, None)
    except usb.core.USBError as e:
        print('detach:', e)
    usb.util.dispose_resources(d)
    for _ in range(100):
        time.sleep(0.2)
        d, itf, proto = find()
        if proto == 2:
            time.sleep(1.0)   # let udev grant access to the new node
            d, itf, proto = find()
            break
    print('after detach protocol', proto)
assert proto == 2
try:
    d.detach_kernel_driver(itf)
except Exception:
    pass
t0 = time.time()
for blk, off in enumerate(range(0, len(image), 4096)):
    d.ctrl_transfer(0x21, 1, blk, itf, image[off:off + 4096], timeout=5000)
    st, poll, state = status(d, itf)
    if st != 0:
        sys.exit(f'block {blk}: status {st} state {state}')
print('wrote', len(image), 'bytes in %.1fs' % (time.time() - t0))
d.ctrl_transfer(0x21, 1, len(range(0, len(image), 4096)), itf, None)   # zero-length: done
try:
    print('final status', status(d, itf))
except usb.core.USBError as e:
    print('final status:', e)
