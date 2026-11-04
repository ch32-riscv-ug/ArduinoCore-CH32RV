"""The FS library (libraries/FS: the FS / File API and FatFs) built for the host,
with the address and undefined-behaviour sanitizers, on disk images.

- The API's behaviour on a volume the library formats (app.cpp `api`).
- Volumes Linux makes (mkfs.fat FAT12/16/32, mkfs.exfat), with and without
  an MBR partition table: the library writes a sample tree, `fsck` finds the
  volume clean and lists the names Linux reads back, and the library reads
  the tree again.
- Volumes the library formats, checked the same way.
- Two volumes mounted at once.

Built twice: the large-series configuration (exFAT, GPT, two volumes) and the
small one (CH32X035: no exFAT, no format, one volume). Where dosfstools /
exfatprogs / sfdisk are missing, those cases skip.
"""
import os
import pathlib
import shutil
import subprocess

import pytest

REPO = pathlib.Path(__file__).resolve().parents[3]
HERE = pathlib.Path(__file__).resolve().parent
LIB = REPO / "libraries/FS/src"
API = ["String", "Print", "Stream", "Common"]
MB = 1 << 20

SAMPLE = ["README.TXT", "lower.txt", "MixedCase.Txt", "A directory with a long name/inner file.txt",
          "日本語/ファイル.txt", "many/f00.dat", "a.b.c.d", "no_extension", "many/same_prefix_long_name_59.dat",
          "many/big.bin"]


def build(out: pathlib.Path, defines) -> pathlib.Path:
    cc, cxx = shutil.which("gcc"), shutil.which("g++")
    if not cc or not cxx:
        pytest.skip("no host gcc / g++")
    flags = ["-g", "-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", *defines,
             f"-I{HERE / 'host'}", f"-I{REPO / 'cores/arduino'}", f"-I{LIB}"]
    objects = []
    sources = [LIB / "fatfs/ff.c", LIB / "fatfs/ffunicode.c", REPO / "cores/arduino/itoa.c",
               REPO / "cores/arduino/dtostrf.c", REPO / "cores/arduino/ch32rv_format.c"] + sorted(LIB.glob("*.cpp")) + [HERE / "host/arduino_host.cpp",
                                                                                   HERE / "app.cpp"] + [
        REPO / f"cores/arduino/api/{name}.cpp" for name in API] + [REPO / "cores/arduino/ch32rv_printf.cpp"]
    for index, source in enumerate(sources):
        obj = out / f"{index:03d}-{source.stem}.o"
        if source.suffix == ".c":
            command = [cc, *flags, "-w", "-c", str(source), "-o", str(obj)]
        else:
            strict = ["-Wall", "-Wextra", "-Werror"] if source.parent in (LIB, HERE) else ["-w"]
            command = [cxx, "-std=gnu++17", *flags, "-include", str(HERE / "host/prelude.h"), *strict, "-c",
                       str(source), "-o", str(obj)]
        subprocess.run(command, check=True)
        objects.append(str(obj))
    binary = out / "fs-host"
    subprocess.run([cxx, "-fsanitize=address,undefined", *objects, "-o", str(binary)], check=True)
    return binary


CONFIGS = {
    "large": ["-DCH32RV_SERIES_CH32V307"],
    "small": ["-DCH32RV_SERIES_CH32X035"],
}


@pytest.fixture(scope="module", params=sorted(CONFIGS))
def host(request, tmp_path_factory):
    out = tmp_path_factory.mktemp(f"fs-{request.param}")
    return request.param, build(out, CONFIGS[request.param]), out


def run(binary, *args):
    p = subprocess.run([str(binary), *map(str, args)], capture_output=True, text=True, timeout=600)
    print(p.stdout[-4000:])
    assert p.returncode == 0, p.stdout[-3000:] + p.stderr[-3000:]
    return p.stdout


def blank(path: pathlib.Path, size: int) -> pathlib.Path:
    path.unlink(missing_ok=True)
    with open(path, "wb") as f:
        f.truncate(size)
    return path


def need(*tools):
    for t in tools:
        if not shutil.which(t):
            pytest.skip(f"{t} is missing")


def linux_volume(image: pathlib.Path, kind: str, size: int, partitioned: bool) -> int:
    """Make `kind` with Linux's mkfs; returns the byte offset of the volume."""
    need("mkfs.fat", "mkfs.exfat", "sfdisk")
    blank(image, size)
    offset = 0
    if partitioned:
        offset = 2048 * 512
        types = {"fat12": "01", "fat16": "06", "fat32": "0c", "exfat": "07"}
        subprocess.run(["sfdisk", "-q", str(image)], input=f"start=2048, type={types[kind]}\n", text=True,
                       check=True, capture_output=True)
    kib = (size - offset) // 1024
    if kind == "exfat":
        part = image.with_suffix(".part")
        blank(part, size - offset)
        subprocess.run(["mkfs.exfat", str(part)], check=True, capture_output=True)
        with open(image, "r+b") as dst, open(part, "rb") as src:
            dst.seek(offset)
            shutil.copyfileobj(src, dst)
        part.unlink()
    else:
        bits = {"fat12": "12", "fat16": "16", "fat32": "32"}[kind]
        extra = ["-s", "1"] if kind == "fat32" else []
        subprocess.run(["mkfs.fat", "-F", bits, *extra, "--offset", str(offset // 512), str(image), str(kib)],
                       check=True, capture_output=True)
    return offset


def fsck(image: pathlib.Path, offset: int, kind: str) -> str:
    """fsck the volume read-only; returns what it lists (names Linux sees)."""
    part = image.with_suffix(".check")
    with open(image, "rb") as src, open(part, "wb") as dst:
        src.seek(offset)
        shutil.copyfileobj(src, dst)
    try:
        if kind == "exfat":
            p = subprocess.run(["fsck.exfat", "-n", str(part)], capture_output=True, text=True)
            assert p.returncode == 0 and "clean" in p.stdout, p.stdout + p.stderr
            return p.stdout
        p = subprocess.run(["fsck.fat", "-n", "-V", "-l", str(part)], capture_output=True, text=True)
        assert p.returncode == 0, p.stdout + p.stderr
        for bad in ("differs", "Free cluster summary wrong", "orphan", "Bad ", "invalid"):
            assert bad not in p.stdout, p.stdout
        return p.stdout
    finally:
        part.unlink()


def test_api(host):
    name, binary, out = host
    image = blank(out / "api.img", 4 * MB)
    if name == "small":   # no format() there: Linux makes the volume
        need("mkfs.fat")
        subprocess.run(["mkfs.fat", str(image)], check=True, capture_output=True)
    out = run(binary, image, "api")
    assert "FAIL" not in out


@pytest.mark.parametrize("kind,size,partitioned", [
    ("fat12", 2 * MB, False), ("fat16", 32 * MB, False), ("fat32", 64 * MB, False), ("exfat", 64 * MB, False),
    ("fat12", 4 * MB, True), ("fat16", 64 * MB, True), ("fat32", 128 * MB, True), ("exfat", 128 * MB, True),
])
def test_linux_volume(host, kind, size, partitioned):
    name, binary, out = host
    image = out / f"linux-{kind}-{int(partitioned)}.img"
    offset = linux_volume(image, kind, size, partitioned)
    if kind == "exfat" and name == "small":
        # exFAT is not built in on the small series: it must refuse cleanly.
        p = subprocess.run([str(binary), str(image), "verify"], capture_output=True, text=True, timeout=60)
        assert "FAIL mount" in p.stdout and p.returncode == 1, p.stdout + p.stderr
        return
    run(binary, image, "populate")
    listing = fsck(image, offset, kind)
    if kind != "exfat":
        # fsck.fat prints an all-lowercase 8.3 name in capitals: it ignores the
        # case bits Windows and Linux's vfat read ("lower.txt" is LOWER.TXT).
        for n in SAMPLE:
            assert f"/{n}".lower() in listing.lower(), f"{n} not listed by fsck.fat:\n{listing[-2000:]}"
    run(binary, image, "verify")


@pytest.mark.parametrize("size", [32 * 1024, 2 * MB, 40 * MB, 600 * MB])
def test_own_format(host, size):
    name, binary, out = host
    need("fsck.fat")
    image = blank(out / f"own-{size}.img", size)
    if name == "small":
        p = subprocess.run([str(binary), str(image), "format"], capture_output=True, text=True, timeout=60)
        assert "FAIL format" in p.stdout, p.stdout   # format() is not built in there
        return
    run(binary, image, "format")
    offset = 0
    if size >= 32 * MB:   # an MBR partition from 32 MB
        with open(image, "rb") as f:
            mbr = f.read(512)
        assert mbr[510:512] == b"\x55\xaa"
        offset = int.from_bytes(mbr[454:458], "little") * 512
        assert offset > 0
    kind = "exfat" if open(image, "rb").read(offset + 11)[offset + 3:offset + 11] == b"EXFAT   " else "fat"
    fsck(image, offset, kind)
    run(binary, image, "verify")


def test_two_volumes(host):
    name, binary, out = host
    a, b = blank(out / "a.img", 4 * MB), blank(out / "b.img", 4 * MB)
    if name == "small":
        need("mkfs.fat")
        for img in (a, b):
            subprocess.run(["mkfs.fat", str(img)], check=True, capture_output=True)
        p = subprocess.run([str(binary), str(a), "twovol", str(b)], capture_output=True, text=True, timeout=60)
        assert "FAIL mount both" in p.stdout, p.stdout   # one volume at a time there
        return
    run(binary, a, "twovol", b)
