"""Isochronous transfers for the PC test host. pyusb has none, so this drives the same libusb
(through ctypes) on the handle pyusb already opened: one transfer of `packets` packets at a time,
completed synchronously."""
import ctypes


class _IsoPacket(ctypes.Structure):
    _fields_ = [("length", ctypes.c_uint), ("actual_length", ctypes.c_uint), ("status", ctypes.c_int)]


def _transfer_type(packets):
    class Transfer(ctypes.Structure):
        _fields_ = [("dev_handle", ctypes.c_void_p), ("flags", ctypes.c_uint8), ("endpoint", ctypes.c_uint8),
                    ("type", ctypes.c_uint8), ("timeout", ctypes.c_uint), ("status", ctypes.c_int),
                    ("length", ctypes.c_int), ("actual_length", ctypes.c_int), ("callback", ctypes.c_void_p),
                    ("user_data", ctypes.c_void_p), ("buffer", ctypes.c_void_p),
                    ("num_iso_packets", ctypes.c_int), ("iso", _IsoPacket * packets)]
    return Transfer


class _Timeval(ctypes.Structure):
    _fields_ = [("tv_sec", ctypes.c_long), ("tv_usec", ctypes.c_long)]


_LIBUSB_TRANSFER_TYPE_ISOCHRONOUS = 1


def iso_transfer(dev, endpoint, packet_size, packets, data=None, timeout_ms=3000, statuses=None):
    """Run one isochronous transfer on `dev` (a pyusb device with the interface claimed and the
    alternate setting selected). IN: returns the list of packet payloads. OUT: `data` is cut into
    `packets` packets of `packet_size`; returns the bytes sent per packet. A list passed as
    `statuses` gets each packet's libusb status (0: arrived whole; an IN packet the host received
    with an error comes back empty and non-zero)."""
    dev._ctx.managed_open()
    backend = dev._ctx.backend
    lib, handle = backend.lib, dev._ctx.handle.handle
    Transfer = _transfer_type(packets)
    lib.libusb_alloc_transfer.restype = ctypes.c_void_p
    lib.libusb_alloc_transfer.argtypes = [ctypes.c_int]
    lib.libusb_submit_transfer.argtypes = [ctypes.c_void_p]
    lib.libusb_free_transfer.argtypes = [ctypes.c_void_p]
    lib.libusb_handle_events_timeout.argtypes = [ctypes.c_void_p, ctypes.POINTER(_Timeval)]
    lib.libusb_dev_mem_alloc.restype = ctypes.c_void_p
    lib.libusb_dev_mem_alloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    lib.libusb_dev_mem_free.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
    h = handle.value if hasattr(handle, "value") else handle
    size = packet_size * packets
    raw = lib.libusb_alloc_transfer(packets)
    if not raw:
        raise OSError("libusb_alloc_transfer failed")
    t = ctypes.cast(raw, ctypes.POINTER(Transfer)).contents
    # The buffer from usbfs's mmap (libusb_dev_mem_alloc), taken once: otherwise usbfs kmallocs each isochronous URB's
    # buffer in one piece, and 42 intervals of 48 KiB (2 MiB) failed with ENOMEM whenever the kernel's memory was
    # fragmented (errno 12, which libusb reports as -1 for a split transfer; 8 intervals passed; usbmon showed no URB,
    # 2026-11-21). Plain memory where the kernel offers no mmap.
    mem = lib.libusb_dev_mem_alloc(h, size)
    buffer = ctypes.cast(mem, ctypes.POINTER(ctypes.c_char * size)).contents if mem else ctypes.create_string_buffer(size)
    if data is not None:
        ctypes.memmove(buffer, bytes(data).ljust(size, b"\0"), size)
    done = []
    callback = ctypes.CFUNCTYPE(None, ctypes.c_void_p)(lambda _p: done.append(True))
    t.dev_handle = h
    t.endpoint = endpoint
    t.type = _LIBUSB_TRANSFER_TYPE_ISOCHRONOUS
    t.timeout = timeout_ms
    t.length = size
    t.buffer = ctypes.cast(buffer, ctypes.c_void_p).value if not mem else mem
    t.num_iso_packets = packets
    t.callback = ctypes.cast(callback, ctypes.c_void_p)
    for i in range(packets):
        t.iso[i].length = packet_size
    try:
        rc = lib.libusb_submit_transfer(raw)
        if rc != 0:
            raise OSError(f"libusb_submit_transfer: {rc}")
        tv = _Timeval(0, 100000)
        while not done:
            lib.libusb_handle_events_timeout(backend.ctx, ctypes.byref(tv))
        if t.status != 0:
            raise OSError(f"isochronous transfer status {t.status}")
        out = []
        for i in range(packets):
            n = t.iso[i].actual_length
            out.append(ctypes.string_at(ctypes.addressof(buffer) + i * packet_size, n) if data is None else n)
            if statuses is not None:
                statuses.append(t.iso[i].status)
        return out
    finally:
        lib.libusb_free_transfer(raw)
        if mem:
            lib.libusb_dev_mem_free(h, mem, size)
