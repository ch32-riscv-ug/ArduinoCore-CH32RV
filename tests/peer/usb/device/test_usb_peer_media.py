"""usb.peer-media: the DUT (tests/sketch_support/usb_media - a UAC1 headset,
32 kHz 16-bit mono both ways, and a UVC camera, 80x60 YUY2) as a device, the
ESP32-S3 (EspUsbHost) as the host.

What the S3 cannot do, and so what is not asserted here:
  - a bulk UVC stream: EspUsbHost 2.10.0 starts one but never reads it, so the
    camera is built isochronous (build_opt.h beside the staged sketch); the PC
    test covers bulk;
  - stopping the microphone (there is no audioInputStop()), and with it
    running, every speaker packet: with both isochronous streams up the S3
    drops OUT packets - and camera packets, the camera's being isochronous
    too. So the microphone runs last;
  - polling the microphone every frame: it takes about 46 of the 64 KB/s the
    device offers (the DUT refuses no write - it sends what it is asked for).
    The rate is recorded, continuity is asserted.
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from s3_peer import command, dut_ready, settings, wait_enumerated


def test_usb_peer_media(dut, peers, record_property):
    s3 = peers["s3"]
    build_id = settings(__file__)["build_id"]
    dut_ready(dut, build_id)
    dev = wait_enumerated(s3, build_id)
    assert {(0x01, 0x01), (0x01, 0x02), (0x0E, 0x01), (0x0E, 0x02)} <= set(dev["interfaces"])

    # Speaker: half a second of a ramp. EspUsbHost keeps the stream going with
    # silence around it, so the DUT counts at least what was sent, and its byte
    # sum - which silence leaves alone - is exactly the ramp's.
    dut.write("audio\n")
    before = dut.expect(rb"AUDIO (\d+) (\d+) \d+\r?\n", timeout=5)
    m = command(s3, "audioout 500", rb"AUDIOOUT (\d) (\d+) (\d+)", timeout=10)
    assert m.group(1) == b"1" and int(m.group(2)) == 32000
    dut.write("audio\n")
    after = dut.expect(rb"AUDIO (\d+) (\d+) \d+\r?\n", timeout=5)
    assert int(after.group(1)) - int(before.group(1)) >= int(m.group(2))
    assert (int(after.group(2)) - int(before.group(2))) & 0xFFFFFFFF == int(m.group(3))

    # Camera: whole 80x60 frames with the DUT's pattern.
    m = command(s3, "video 3000", rb"VIDEO (\d+) (\d+) (\d+) (\d+)", timeout=15)
    record_property("video_frames_per_3s", int(m.group(1)))
    assert int(m.group(1)) >= 5 and int(m.group(2)) == 0
    assert (int(m.group(3)), int(m.group(4))) == (80, 60)

    # Microphone: a second of the DUT's ramp, without a break.
    m = command(s3, "audioin 1000", rb"AUDIOIN (\d+) (\d+)", timeout=10)
    record_property("audio_capture_bytes_per_s", int(m.group(1)))
    assert int(m.group(1)) >= 32000 and int(m.group(2)) == 0
