"""usb.peer-host-fs: the DUT (usb_peer_host.ino built with USB_HOST_FS, CH32RVUsbHost) as the USB host, the
RP2040 (host_storage/peer_pico) as the device; CH32RVUsbHostMscFS puts a FAT volume on its mass storage.

Unit 0 (64 blocks of RAM on the peer) is made an empty FAT12 volume by the peer (`fat12`: the small series' FS has
no format()) and mounted; two files are written, read back and listed; one is removed; end() closes the
volume (SYNCHRONIZE CACHE) and a second mount without formatting finds what is left, byte for byte. Unit 1 is too
small for a FAT volume (16 blocks): the mount fails there and leaves the stick usable. Same silicon requirement as
usb-peer-host."""
import pathlib
import secrets
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from host_peer import Peer, device, dut_ready, host, settings, wait_device


def test_usb_peer_host_fs(dut, peers):
    peer = Peer(peers)
    dut_ready(dut, settings(__file__)["build_id"])
    wait_device(dut, peer.id["storage"])
    device(peer, "?", rb"READY 1")

    # The peer makes unit 0 an empty FAT12 volume: the small series' FS has no format() (FS_SMALL_FLASH).
    device(peer, "fat12", rb"FAT12 1")
    m = host(dut, "fsmount 0 0", rb"FSMOUNT (\d) (\d) (\d+)", timeout=30)
    assert m.group(1) == b"1", "the stick's unit 0 (a FAT12 volume) did not mount"
    assert m.group(2) == b"1", f"the volume is FAT12, the DUT says FAT type {m.group(2).decode()}"
    assert 0 < int(m.group(3)) <= 64 * 512

    first, second = secrets.token_bytes(160), secrets.token_bytes(40)   # a command line holds 192 bytes
    assert host(dut, f"fswrite A.BIN {first.hex()}", rb"FSWRITE (\d+)", timeout=20).group(1) == b"160"
    assert host(dut, f"fswrite LONGNAME.TXT {second.hex()}", rb"FSWRITE (\d+)", timeout=20).group(1) == b"40"
    m = host(dut, "fsread A.BIN", rb"FSREAD (\d) (\w*)", timeout=20)
    assert m.group(1) == b"1" and bytes.fromhex(m.group(2).decode()) == first
    listing = host(dut, "fsls", rb"FSLS([^\r\n]*)", timeout=20).group(1).decode().split()
    assert sorted(listing) == ["A.BIN:160", "LONGNAME.TXT:40"], listing
    assert host(dut, "fsrm A.BIN", rb"FSRM (\d)", timeout=20).group(1) == b"1"
    host(dut, "fsend", rb"FSEND", timeout=20)

    # Mounted again, not formatted: what was left is there.
    assert host(dut, "fsmount 0 0", rb"FSMOUNT (\d) \d \d+", timeout=30).group(1) == b"1"
    assert host(dut, "fsls", rb"FSLS([^\r\n]*)", timeout=20).group(1).decode().split() == ["LONGNAME.TXT:40"]
    m = host(dut, "fsread LONGNAME.TXT", rb"FSREAD (\d) (\w*)", timeout=20)
    assert m.group(1) == b"1" and bytes.fromhex(m.group(2).decode()) == second
    assert host(dut, "fsread A.BIN", rb"FSREAD (\d) \w*", timeout=20).group(1) == b"0"
    host(dut, "fsend", rb"FSEND", timeout=20)

    # Unit 1, 16 blocks: no volume fits, and the stick still answers after the failed mount.
    assert host(dut, "fsmount 1 1", rb"FSMOUNT (\d) \d \d+", timeout=30).group(1) == b"0"
    assert host(dut, "msclun 0", rb"MSCLUN (\d)").group(1) == b"1"
    assert host(dut, "msc", rb"MSC (\d) [^\r\n]*", timeout=20).group(1) == b"1"
