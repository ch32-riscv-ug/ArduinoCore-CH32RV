#!/usr/bin/env python3
"""W-5 prototype: build a Board Manager platform archive and package index.

- Packages the platform entries (PLATFORM_ENTRIES) as a .tar.bz2 (single root folder).
- In the packaged platform.txt, compiler.path is rewritten to the installed
  tool ({runtime.tools...}); the working tree keeps the PATH/-override default
  for symlink-mode development.
- Emits package_ch32-riscv-ug_index.json referencing the archive at --base-url.
- Tool section: --tools github uses tools/index/tools_xpack_gcc.json
  (direct links to xPack GitHub Releases); --tools local rewrites the URL of
  the current host's entry to --base-url (archive must be served there) while
  keeping the official checksum.

Usage:
  gen_index.py --platform <dir> --out <dir> --base-url http://127.0.0.1:8000 \
               [--tools github|local] [--version 0.0.1]
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import tarfile
import tempfile

TOOL_NAME = "xpack-riscv-none-elf-gcc"
TOOL_VERSION = "14.3.0-1"
PACKAGER = "ch32-riscv-ug"
ARCH = "ch32v"

COMPILER_PATH_DEV = "compiler.path="
COMPILER_PATH_PKG = ("compiler.path={runtime.tools." + TOOL_NAME + ".path}/bin/")


def sha256(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


# The repo root doubles as the Arduino platform directory (R-15 method A), so a
# release archive is an allowlist, not the whole tree. Keep in sync with
# PLATFORM_ENTRIES in tests/compile/test_compile.sh.
# Arduino platform files that a release archive must carry. Entries absent from the
# tree are skipped, so this can list things the platform does not have yet.
PLATFORM_ENTRIES = (
    "platform.txt",
    "boards.txt",
    "programmers.txt",
    "cores",
    "variants",
    "libraries",
    "bootloaders",
    "system",
)
REQUIRED_ENTRIES = ("platform.txt", "boards.txt", "cores", "variants")


def build_archive(platform_dir: pathlib.Path, out: pathlib.Path, version: str) -> pathlib.Path:
    root = f"ArduinoCore-CH32-{ARCH}-{version}"
    archive = out / f"{root}.tar.bz2"
    with tempfile.TemporaryDirectory() as tmp:
        staged = pathlib.Path(tmp) / root
        staged.mkdir(parents=True)
        for name in PLATFORM_ENTRIES:
            src = platform_dir / name
            if not src.exists():
                if name in REQUIRED_ENTRIES:
                    raise SystemExit(f"required platform entry missing: {src}")
                continue
            if src.is_dir():
                shutil.copytree(src, staged / name, symlinks=False)
            else:
                shutil.copy2(src, staged / name)
        ptxt = staged / "platform.txt"
        text = ptxt.read_text(encoding="utf-8")
        lines = [COMPILER_PATH_PKG if line == COMPILER_PATH_DEV else line
                 for line in text.splitlines()]
        if COMPILER_PATH_PKG not in lines:
            raise SystemExit("platform.txt: expected exact line 'compiler.path=' to rewrite")
        ptxt.write_text("\n".join(lines) + "\n", encoding="utf-8")
        with tarfile.open(archive, "w:bz2") as tar:
            tar.add(staged, arcname=root, recursive=True)
    return archive


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--platform", required=True, type=pathlib.Path)
    ap.add_argument("--out", required=True, type=pathlib.Path)
    ap.add_argument("--base-url", required=True)
    ap.add_argument("--tools", choices=["github", "local"], default="github")
    ap.add_argument("--version", default="0.0.1")
    args = ap.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    archive = build_archive(args.platform, args.out, args.version)

    tool = json.loads((pathlib.Path(__file__).parent / "tools_xpack_gcc.json")
                      .read_text(encoding="utf-8"))
    systems = tool["systems"]
    if args.tools == "local":
        systems = [dict(s, url=f"{args.base_url}/{s['archiveFileName']}")
                   for s in systems]

    index = {
        "packages": [{
            "name": PACKAGER,
            "maintainer": "CH32 RISC-V UG (prototype, not affiliated with WCH)",
            "websiteURL": "https://github.com/ch32-riscv-ug/ArduinoCore-CH32",
            "email": "",
            "help": {"online": "https://github.com/ch32-riscv-ug/ArduinoCore-CH32"},
            "platforms": [{
                "name": "CH32 RISC-V (prototype)",
                "architecture": ARCH,
                "version": args.version,
                "category": "Contributed",
                "url": f"{args.base_url}/{archive.name}",
                "archiveFileName": archive.name,
                "checksum": f"SHA-256:{sha256(archive)}",
                "size": str(archive.stat().st_size),
                "boards": [{"name": "CH32V00X"}],
                "toolsDependencies": [{
                    "packager": PACKAGER,
                    "name": TOOL_NAME,
                    "version": TOOL_VERSION,
                }],
            }],
            "tools": [{
                "name": TOOL_NAME,
                "version": TOOL_VERSION,
                "systems": systems,
            }],
        }],
    }
    index_path = args.out / f"package_{PACKAGER}_index.json"
    index_path.write_text(json.dumps(index, indent=2) + "\n", encoding="utf-8")
    print(f"wrote: {archive}")
    print(f"wrote: {index_path}")


if __name__ == "__main__":
    main()
