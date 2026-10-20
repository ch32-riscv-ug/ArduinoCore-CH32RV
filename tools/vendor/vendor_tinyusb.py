#!/usr/bin/env -S uv run --no-project --script
# /// script
# requires-python = ">=3.10"
# ///
"""libraries/CH32RVTinyUSB/src is a pinned TinyUSB snapshot.

[ADR-0012](../../docs/adr/0012-usb-stack.ja.md) adopts TinyUSB and keeps it
inside the repository rather than as an external dependency, because we expect
to carry patches - an unmerged upstream PR, or a fix we have not landed yet.

That makes a lock file necessary rather than nice to have. Without one, a local
edit is indistinguishable from upstream code, and the next version bump reverts
it silently. So every vendored file is hashed, and every deliberate difference
from upstream has to be listed as a patch.

  uv run tools/vendor/vendor_tinyusb.py --update      # fetch and re-vendor
  uv run tools/vendor/vendor_tinyusb.py --check       # verify against the lock

--check is offline: it hashes what is on disk and compares. --update needs the
network. Offline integrity checks also live in tests/platform/test_source_locks.py.

Not all of upstream src/ is vendored: portable/ carries a driver for every
vendor TinyUSB supports, and shipping thirty of them to compile into nothing
helps nobody. The subset is spelled out in KEEP below, and the lock records
exactly what came in.
"""
import argparse
import hashlib
import io
import pathlib
import shutil
import sys
import tarfile
import urllib.request

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
LOCK = REPO / "vendor" / "tinyusb.lock.toml"
DEST = REPO / "libraries" / "CH32RVTinyUSB" / "src"

# The pinned upstream revision. A release tag when one carries what we need;
# otherwise a master commit, and REF_NOTE says why.
REF = "de4940525876259efb17f14e775312365bfdf97c"
REF_NOTE = ("master at the merge of PR #3703 (CH32X035/X033 USBFS port, "
            "2026-09-27); no release carries it yet - 0.21.0 is older")
URL = f"https://github.com/hathach/tinyusb/archive/{REF}.tar.gz"

# Which of upstream src/ comes in. Everything outside portable/ is generic and
# small; inside portable/ only the drivers a CH32 build can reach.
#   wch              - the CH32 device (FS/HS) and host (FS) drivers
#   st/stm32_fsdev   - CH32V20x port0 is an ST FSDEV clone and TinyUSB uses
#                      that driver for it
KEEP_PORTABLE = ("portable/wch", "portable/st/stm32_fsdev")

# Files we add next to the vendored tree. They are ours, so the lock ignores
# them - but they must not collide with an upstream path, and a future upstream
# version that adds one of these names has to be noticed, so they are listed.
OURS = ("tusb_config.h", "ch32x035.h", "ch32v10x.h", "ch32v20x.h", "ch32v30x.h", "ch32rv_port.c",
        "ch32rv_dcd_usbfs.c", "ch32rv_dcd_usbhs.c", "ch32rv_dcd_usbd.c", "ch32rv_dcd_dual.c", "ch32rv_dcd_dual.h",
        "ch32rv_hcd_usbfs.c")

# Deliberate differences from upstream, applied by --update and verified by
# --check: the file on disk must contain `after`, and with `after` put back to
# `before` it must hash to the upstream file the lock records. Keep each one
# small and in a place upstream rarely touches, and say why.
PATCHES = (
    {
        "path": "device/usbd.c",
        "before": "static const usbd_class_driver_t _usbd_driver[] = {\n",
        "after": ("static const usbd_class_driver_t _usbd_driver[] = {\n"
                  "#if !CFG_TUD_APP_DRIVERS_ONLY  /* ch32rv patch: see vendor_tinyusb.py */\n"),
        "why": ("CFG_TUD_APP_DRIVERS_ONLY empties the built-in class driver table, so "
                "the drivers come only from usbd_app_driver_get_cb(): a class the "
                "sketch never declares is not referenced and not linked "
                "(CH32RVUsbDevice). Upstream lists every enabled class there."),
    },
    {
        "path": "device/usbd.c",
        "before": "    #endif\n};\n\n// Additional class drivers implemented by application",
        "after": ("    #endif\n#endif  /* !CFG_TUD_APP_DRIVERS_ONLY */\n};\n\n"
                  "// Additional class drivers implemented by application"),
        "why": "closes the #if above",
    },
    {
        "path": "host/usbh.c",
        "before": "static usbh_class_driver_t const usbh_class_drivers[] = {\n",
        "after": ("static usbh_class_driver_t const usbh_class_drivers[] = {\n"
                  "#if !CFG_TUH_APP_DRIVERS_ONLY  /* ch32rv patch: see vendor_tinyusb.py */\n"),
        "why": ("CFG_TUH_APP_DRIVERS_ONLY does for the host what CFG_TUD_APP_DRIVERS_ONLY "
                "does for the device: the class drivers come only from "
                "usbh_app_driver_get_cb() (CH32RVUsbHost), so a sketch links the "
                "host classes it declares and no others."),
    },
    {
        "path": "host/usbh.c",
        "before": "  #endif\n\n};\n\n// Additional class drivers implemented by application",
        "after": ("  #endif\n#endif  /* !CFG_TUH_APP_DRIVERS_ONLY */\n\n};\n\n"
                  "// Additional class drivers implemented by application"),
        "why": "closes the #if above",
    },
    {
        "path": "tusb.c",
        "before": "tusb_role_t _tusb_rhport_role[TUP_USBIP_CONTROLLER_NUM] = { TUSB_ROLE_INVALID };\n",
        "after": ("#if CFG_TUD_ENABLED && CFG_TUH_ENABLED  /* ch32rv patch: see vendor_tinyusb.py */\n"
                  "#pragma weak tud_rhport_init\n#pragma weak tud_inited\n#pragma weak tud_deinit\n"
                  "#pragma weak dcd_int_handler\n#pragma weak usbd_edpt_claim\n"
                  "#pragma weak usbd_edpt_release\n#pragma weak usbd_edpt_xfer\n"
                  "#pragma weak usbd_edpt_xfer_fifo\n"
                  "#pragma weak tuh_rhport_init\n#pragma weak tuh_inited\n#pragma weak tuh_deinit\n"
                  "#pragma weak hcd_int_handler\n#pragma weak usbh_edpt_claim\n"
                  "#pragma weak usbh_edpt_release\n#pragma weak usbh_edpt_xfer_with_callback\n"
                  "#endif\n\n"
                  "tusb_role_t _tusb_rhport_role[TUP_USBIP_CONTROLLER_NUM] = { TUSB_ROLE_INVALID };\n"),
        "why": ("with both stacks enabled, tusb.c refers to each of them (role dispatch, "
                "endpoint streams), so a device-only sketch linked usbh and the HCD "
                "(about 3.5 KB of RAM, too much for a 10 KB CH32V203) and a host-only "
                "sketch linked usbd. Weak references link a stack only when the library "
                "that runs it is used; the other side's branches are never taken then."),
    },
    {
        "path": "common/tusb_private.h",
        "before": "void usbd_edpt_rx_consume(uint8_t rhport, uint8_t ep_addr);\n#else\n",
        "after": ("void usbd_edpt_rx_consume(uint8_t rhport, uint8_t ep_addr);\n"
                  "  #if CFG_TUH_ENABLED  /* ch32rv patch: see vendor_tinyusb.py */\n"
                  "  #pragma weak usbd_edpt_rx_consume\n"
                  "  #endif\n#else\n"),
        "why": ("the same for the inline stream helper the host CDC driver uses: its "
                "device-only branch must not link usbd into a host-only sketch"),
    },
)


def apply_patches(files: dict) -> None:
    for p in PATCHES:
        text = files[p["path"]].decode("utf-8")
        if text.count(p["before"]) != 1:
            raise Failure(f"patch no longer applies to {p['path']}: {p['why']}")
        files[p["path"]] = text.replace(p["before"], p["after"], 1).encode("utf-8")


def unpatched(name: str, blob: bytes) -> bytes:
    """The upstream bytes of a patched file, or None when a patch is missing."""
    text = blob.decode("utf-8")
    for p in reversed([p for p in PATCHES if p["path"] == name]):
        if text.count(p["after"]) != 1:
            return None
        text = text.replace(p["after"], p["before"], 1)
    return text.encode("utf-8")


class Failure(Exception):
    pass


def sha256(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    h.update(path.read_bytes())
    return h.hexdigest()


def wanted(name: str) -> bool:
    """True for an upstream src/ path we vendor. `name` is relative to src/."""
    if not name.startswith("portable/"):
        return True
    return any(name == k or name.startswith(k + "/") for k in KEEP_PORTABLE)


def fetch() -> dict:
    """{relative path: bytes} of the subset, straight from the release tarball."""
    print(f"fetching {URL}", flush=True)
    with urllib.request.urlopen(URL, timeout=120) as response:
        blob = response.read()
    out = {}
    prefix = f"tinyusb-{REF}/src/"
    # The licence sits at the repository root, not in src/, and MIT requires it
    # to travel with the copy - so it comes along as LICENSE inside dest.
    license_path = f"tinyusb-{REF}/LICENSE"
    with tarfile.open(fileobj=io.BytesIO(blob), mode="r:gz") as tar:
        for member in tar.getmembers():
            if not member.isfile():
                continue
            if member.name == license_path:
                out["LICENSE"] = tar.extractfile(member).read()
                continue
            if not member.name.startswith(prefix):
                continue
            name = member.name[len(prefix):]
            if wanted(name):
                out[name] = tar.extractfile(member).read()
    if "LICENSE" not in out:
        raise Failure(f"no {license_path} in the tarball - refusing to vendor "
                      f"MIT code without its licence")
    if not out:
        raise Failure("the tarball had no src/ files - did the layout change?")
    return out


def write_lock(files: dict) -> None:
    lines = [
        "# TinyUSB vendored snapshot lock",
        "# Regenerate/verify: tools/vendor/vendor_tinyusb.py",
        "# See docs/adr/0012-usb-stack.ja.md",
        "",
        "[[source]]",
        'id = "tinyusb"',
        'url = "https://github.com/hathach/tinyusb.git"',
        f'commit = "{REF}"',
        f"# {REF_NOTE}",
        'license = "MIT"',
        'license_file = "libraries/CH32RVTinyUSB/src/LICENSE"',
        f'dest = "{DEST.relative_to(REPO).as_posix()}"',
        "# Only part of upstream src/ is vendored; see KEEP_PORTABLE in the tool.",
        "kept_portable = [" + ", ".join(f'"{k}"' for k in KEEP_PORTABLE) + "]",
        "# Deliberate differences from upstream. Empty means the copy is verbatim.",
        "# Each entry: what it changes and why, so a version bump can re-apply or drop it.",
        "patches = [" + ", ".join(f'"{p["path"]}: {p["why"][:60]}"' for p in PATCHES) + "]",
        "# Files this repository adds next to the vendored tree (not upstream).",
        "ours = [" + ", ".join(f'"{o}"' for o in OURS) + "]",
        "",
        "# SHA-256 of every vendored file, relative to dest.",
        "files = [",
    ]
    for name in sorted(files):
        lines.append(f'  {{ path = "{name}", sha256 = "{files[name]}" }},')
    lines += ["]", ""]
    LOCK.write_text("\n".join(lines), encoding="utf-8")


def read_lock() -> dict:
    if not LOCK.exists():
        raise Failure(f"no lock file at {LOCK}; run with --update first")
    out = {}
    for line in LOCK.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line.startswith("{ path ="):
            path = line.split('"')[1]
            digest = line.split('"')[3]
            out[path] = digest
    if not out:
        raise Failure(f"{LOCK} lists no files")
    return out


def update() -> int:
    files = fetch()
    if DEST.exists():
        shutil.rmtree(DEST)
    hashes = {name: hashlib.sha256(blob).hexdigest() for name, blob in files.items()}
    apply_patches(files)
    for name, blob in files.items():
        target = DEST / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(blob)
    write_lock(hashes)
    print(f"vendored {len(files)} files into {DEST.relative_to(REPO)}")
    print(f"wrote {LOCK.relative_to(REPO)}")
    return 0


def check() -> int:
    locked = read_lock()
    problems = []

    for name, digest in sorted(locked.items()):
        path = DEST / name
        if not path.exists():
            problems.append(f"missing: {name}")
        elif any(p["path"] == name for p in PATCHES):
            upstream = unpatched(name, path.read_bytes())
            if upstream is None:
                problems.append(f"patch missing: {name}")
            elif hashlib.sha256(upstream).hexdigest() != digest:
                problems.append(f"modified: {name} (beyond its listed patches)")
        elif sha256(path) != digest:
            problems.append(f"modified: {name} (not listed in patches)")

    on_disk = {p.relative_to(DEST).as_posix()
               for p in DEST.rglob("*") if p.is_file()}
    for extra in sorted(on_disk - set(locked) - set(OURS)):
        problems.append(f"unlisted: {extra}")

    if problems:
        print(f"{len(problems)} problem(s) against {LOCK.relative_to(REPO)}:",
              file=sys.stderr)
        for p in problems[:20]:
            print(f"  {p}", file=sys.stderr)
        print("\nIf a difference is deliberate, record it in the lock's "
              "patches list; if not, re-run with --update.", file=sys.stderr)
        return 1

    print(f"ok: {len(locked)} vendored files match {LOCK.relative_to(REPO)}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--update", action="store_true",
                    help="fetch the pinned revision and re-vendor (needs network)")
    ap.add_argument("--check", action="store_true",
                    help="verify the working copy against the lock (offline)")
    args = ap.parse_args()
    try:
        if args.update:
            return update()
        return check()
    except Failure as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
