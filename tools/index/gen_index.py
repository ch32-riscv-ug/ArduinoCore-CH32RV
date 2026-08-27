#!/usr/bin/env python3
"""W-5 prototype: build a Board Manager platform archive and package index.

- Packages the platform entries (PLATFORM_ENTRIES) as a .tar.bz2 (single root folder).
- In the packaged platform.txt, compiler.path is rewritten to the installed
  tool ({runtime.tools...}); the working tree keeps the PATH/-override default
  for symlink-mode development.
- Emits package_ch32-riscv-ug_index.json referencing the archive at --base-url.
- Tool section: --tools github uses tools/index/tools_xpack_gcc.json and
  tools_probe_rs.json (direct links to the upstream GitHub Releases); --tools
  local rewrites the URL of the tools named by --local-tools to --base-url
  while keeping the official checksums. Only those, because the archive has to
  actually be served there - a tool nobody staged locally must keep pointing at
  GitHub or the install 404s.
- The board list shown by Board Manager is read out of boards.txt, and the
  version out of platform.txt, so neither can drift from what ships.
- --merge <index.json> keeps the versions an existing published index already
  offers. A Board Manager index is append-only: dropping an old version breaks
  everyone pinned to it, and sketch.yaml profiles pin by version.

Usage:
  gen_index.py --platform <dir> --out <dir> --base-url http://127.0.0.1:8000 \
               [--tools github|local] [--version 0.0.1] [--merge old.json]
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import tarfile
import tempfile

MAINTAINER = "CH32 RISC-V UG"
WEBSITE = "https://github.com/ch32-riscv-ug/ArduinoCore-CH32"
TOOL_NAME = "xpack-riscv-none-elf-gcc"
TOOL_VERSION = "14.3.0-1"
PROBE_TOOL_NAME = "probe-rs"
PROBE_TOOL_VERSION = "0.32.0"
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
# PLATFORM_ENTRIES in tests/compile/compile_matrix.py.
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
    "debug",
)
REQUIRED_ENTRIES = ("platform.txt", "boards.txt", "cores", "variants")


def platform_version(platform_dir: pathlib.Path) -> str:
    """The version platform.txt declares. The index must agree with it: Board
    Manager installs by the index's version but the IDE shows platform.txt's."""
    for line in (platform_dir / "platform.txt").read_text(encoding="utf-8").splitlines():
        if line.startswith("version="):
            return line.split("=", 1)[1].strip()
    raise SystemExit("platform.txt has no version= line")


def board_names(platform_dir: pathlib.Path) -> list:
    """Board Manager shows this list under the platform, so it has to be the
    real one. boards.txt is generated, so reading it keeps the two in step."""
    names = []
    for line in (platform_dir / "boards.txt").read_text(encoding="utf-8").splitlines():
        key, _, value = line.partition("=")
        # `<ID>.name=` only; menu entries are `<ID>.menu.pnum.<PN>=`.
        if key.endswith(".name") and key.count(".") == 1 and value:
            names.append({"name": value.strip()})
    if not names:
        raise SystemExit("boards.txt lists no boards")
    return names


def merge_previous(index: dict, previous: pathlib.Path) -> dict:
    """Carry forward every platform and tool version the old index offered.

    A Board Manager index is append-only. Regenerating from scratch would drop
    older versions, which uninstalls nobody but makes them unreinstallable -
    and every sketch.yaml profile in the wild pins a version.
    """
    old = json.loads(previous.read_text(encoding="utf-8"))
    old_pkg = next((p for p in old.get("packages", []) if p["name"] == PACKAGER), None)
    if old_pkg is None:
        return index
    pkg = index["packages"][0]
    for key, ident in (("platforms", ("architecture", "version")),
                       ("tools", ("name", "version"))):
        fresh = {tuple(e[k] for k in ident): e for e in pkg[key]}
        merged = [e for e in old_pkg.get(key, [])
                  if tuple(e.get(k) for k in ident) not in fresh]
        # Newest last is what the Arduino index files do; arduino-cli sorts
        # semver itself, so this is presentation only.
        pkg[key] = merged + pkg[key]
    return index


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


def main(argv=None) -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--platform", required=True, type=pathlib.Path)
    ap.add_argument("--out", required=True, type=pathlib.Path)
    ap.add_argument("--base-url", required=True)
    ap.add_argument("--tools", choices=["github", "local"], default="github")
    ap.add_argument("--local-tools", default=TOOL_NAME,
                    help="comma-separated tool names to point at --base-url "
                         f"when --tools local (default: {TOOL_NAME})")
    ap.add_argument("--version", help="default: the version in platform.txt")
    ap.add_argument("--merge", type=pathlib.Path,
                    help="existing index whose older versions to keep")
    args = ap.parse_args(argv)

    declared = platform_version(args.platform)
    if args.version and args.version != declared:
        raise SystemExit(f"--version {args.version} does not match platform.txt "
                         f"version={declared}; bump platform.txt instead")
    args.version = declared

    args.out.mkdir(parents=True, exist_ok=True)
    archive = build_archive(args.platform, args.out, args.version)

    here = pathlib.Path(__file__).parent
    tool = json.loads((here / "tools_xpack_gcc.json").read_text(encoding="utf-8"))
    probe = json.loads((here / "tools_probe_rs.json").read_text(encoding="utf-8"))
    systems = tool["systems"]
    # The mirror records where each archive came from and whether it was
    # repacked. That is provenance for readers of the fragment, not part of the
    # Board Manager schema, so it stays out of the published index.
    drop = {"upstreamUrl", "upstreamArchiveFileName", "upstreamChecksum", "repacked"}
    probe_systems = [{k: v for k, v in e.items() if k not in drop}
                     for e in probe["systems"]]
    if args.tools == "local":
        wanted = {n.strip() for n in args.local_tools.split(",") if n.strip()}
        unknown = wanted - {TOOL_NAME, PROBE_TOOL_NAME}
        if unknown:
            raise SystemExit(f"--local-tools: unknown tool {sorted(unknown)}")

        def localize(entries):
            return [dict(e, url=f"{args.base_url}/{e['archiveFileName']}")
                    for e in entries]
        if TOOL_NAME in wanted:
            systems = localize(systems)
        if PROBE_TOOL_NAME in wanted:
            probe_systems = localize(probe_systems)

    index = {
        "packages": [{
            "name": PACKAGER,
            "maintainer": MAINTAINER,
            "websiteURL": WEBSITE,
            "email": "",
            "help": {"online": f"{WEBSITE}/issues"},
            "platforms": [{
                "name": "CH32 RISC-V (prototype)",
                "architecture": ARCH,
                "version": args.version,
                "category": "Contributed",
                "url": f"{args.base_url}/{archive.name}",
                "archiveFileName": archive.name,
                "checksum": f"SHA-256:{sha256(archive)}",
                "size": str(archive.stat().st_size),
                "help": {"online": f"{WEBSITE}/issues"},
                "boards": board_names(args.platform),
                "toolsDependencies": [
                    {"packager": PACKAGER, "name": TOOL_NAME,
                     "version": TOOL_VERSION},
                    {"packager": PACKAGER, "name": PROBE_TOOL_NAME,
                     "version": PROBE_TOOL_VERSION},
                ],
            }],
            "tools": [
                {"name": TOOL_NAME, "version": TOOL_VERSION, "systems": systems},
                {"name": PROBE_TOOL_NAME, "version": PROBE_TOOL_VERSION,
                 "systems": probe_systems},
            ],
        }],
    }
    if args.merge:
        index = merge_previous(index, args.merge)
    index_path = args.out / f"package_{PACKAGER}_index.json"
    index_path.write_text(json.dumps(index, indent=2) + "\n", encoding="utf-8")
    print(f"wrote: {archive}")
    pkg = index["packages"][0]
    print(f"wrote: {index_path}")
    print(f"  platform {args.version} with {len(pkg['platforms'][-1]['boards'])} boards; "
          f"index offers {len(pkg['platforms'])} platform / {len(pkg['tools'])} tool entries")


# Callers that already are Python (tools/index/install_check.py) use this
# rather than paying for a subprocess and parsing printed paths back out.
main_with_args = main


if __name__ == "__main__":
    main()
