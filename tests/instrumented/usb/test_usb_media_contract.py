"""usb.audio-capture, usb.audio-playback, usb.video-bulk.

The DUT runs tests/sketch_support/usb_media: a UAC1 headset (32 kHz 16-bit mono each way) and a
UVC camera (80x60 YUY2, bulk). The host side drives them below the OS - the audio and video
drivers are detached, isochronous transfers go through usb_iso.py - so no sound card or video
device access is needed.
"""
import json
import pathlib
import secrets
import struct
import sys
import time

import usb.util

# The runner stages the helpers beside this file; pytest imports tests without adding their directory.
sys.path.insert(0, str(pathlib.Path(__file__).parent))
from usb_host import APP, PcUsbHost  # noqa: E402
from usb_iso import iso_transfer  # noqa: E402


def test_usb_media_contract(dut, record_property):
    settings = json.loads(pathlib.Path(__file__).with_name("case.json").read_text())
    build_id = settings["build_id"]
    host = PcUsbHost(settings["target"]["dut_usb_topology"])

    nonce = secrets.token_hex(4)
    dut.write(f"ready {nonce}\n")
    m = dut.expect(rb"READY " + nonce.encode() + rb" (\S+) (\d) (\S+)\r?\n", timeout=15)
    assert m.group(1).decode() == build_id
    assert m.group(2) == b"1", "USBDevice.begin() failed: " + m.group(3).decode()
    assert host.wait_ids(APP, 10), "application device did not enumerate"
    deadline = time.monotonic() + 5
    while not host.product().endswith(build_id) and time.monotonic() < deadline:
        time.sleep(0.05)
    assert host.product().endswith(build_id), "the device is not this build: " + host.product()

    dev = host.device()
    cfg = dev.get_active_configuration()
    numbers = sorted({i.bInterfaceNumber for i in cfg})
    usb.util.dispose_resources(dev)
    with host.claimed(numbers) as dev:
        cfg = dev.get_active_configuration()
        streams = [i for i in cfg if (i.bInterfaceClass, i.bInterfaceSubClass, i.bAlternateSetting) == (1, 2, 1)]
        mic = next(i for i in streams if i[0].bEndpointAddress & 0x80)
        speaker = next(i for i in streams if not i[0].bEndpointAddress & 0x80)

        # usb.audio-capture: 200 ms of microphone, a continuous ramp, one 64-byte packet per frame.
        dev.set_interface_altsetting(interface=mic.bInterfaceNumber, alternate_setting=1)
        time.sleep(0.05)
        packets = iso_transfer(dev, mic[0].bEndpointAddress, mic[0].wMaxPacketSize, 200)
        dev.set_interface_altsetting(interface=mic.bInterfaceNumber, alternate_setting=0)
        data = b"".join(packets)
        samples = struct.unpack(f"<{len(data) // 2}H", data[:len(data) // 2 * 2])
        assert len(samples) > 32 * 150, f"only {len(samples)} samples in 200 ms"
        breaks = [k for k in range(len(samples) - 1) if (samples[k + 1] - samples[k]) & 0xFFFF != 1]
        assert not breaks, f"the ramp breaks at {breaks[:5]}"
        record_property("audio_capture_bytes_per_200ms", len(data))

        # usb.audio-playback: 100 ms of speaker data arrives whole.
        dut.write("audio\n")
        before = dut.expect(rb"AUDIO (\d+) (\d+) \d+\r?\n", timeout=5)
        payload = secrets.token_bytes(6400)
        dev.set_interface_altsetting(interface=speaker.bInterfaceNumber, alternate_setting=1)
        sent = iso_transfer(dev, speaker[0].bEndpointAddress, 64, 100, data=payload)
        time.sleep(0.2)
        dev.set_interface_altsetting(interface=speaker.bInterfaceNumber, alternate_setting=0)
        assert sum(sent) == len(payload)
        dut.write("audio\n")
        after = dut.expect(rb"AUDIO (\d+) (\d+) \d+\r?\n", timeout=5)
        assert int(after.group(1)) - int(before.group(1)) == len(payload)
        assert (int(after.group(2)) - int(before.group(2))) & 0xFFFFFFFF == sum(payload)

        # usb.video-bulk: probe/commit, then whole frames whose every byte is (i * 7 + f) & 0xFF.
        vs = next(i for i in cfg if (i.bInterfaceClass, i.bInterfaceSubClass) == (0x0E, 2))
        ep = vs[0]
        probe = bytes(dev.ctrl_transfer(0xA1, 0x81, 0x0100, vs.bInterfaceNumber, 48))
        dev.ctrl_transfer(0x21, 0x01, 0x0100, vs.bInterfaceNumber, probe)
        probe = bytes(dev.ctrl_transfer(0xA1, 0x81, 0x0100, vs.bInterfaceNumber, len(probe)))
        dev.ctrl_transfer(0x21, 0x01, 0x0200, vs.bInterfaceNumber, probe)
        frames, current = [], bytearray()
        deadline = time.monotonic() + 5
        while len(frames) < 4 and time.monotonic() < deadline:
            p = bytes(ep.read(ep.wMaxPacketSize, timeout=1000))
            current += p[p[0]:]
            if p[1] & 0x02:                  # end of frame
                frames.append(bytes(current))
                current = bytearray()
        dev.ctrl_transfer(0x02, 0x01, 0, ep.bEndpointAddress)   # CLEAR_FEATURE(HALT) ends the stream
        assert len(frames) == 4, "the camera did not deliver frames"
        for f in frames[1:]:                 # the first may have started before the commit
            assert len(f) == 80 * 60 * 2
            assert all(b == ((i * 7 + f[0]) & 0xFF) for i, b in enumerate(f)), "a frame is corrupt"
        dut.write("video\n")
        assert int(dut.expect(rb"VIDEO (\d+) [01]\r?\n", timeout=5).group(1)) >= 4
