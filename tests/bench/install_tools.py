#!/usr/bin/env -S uv run --no-project --script
# /// script
# requires-python = ">=3.10"
# ///
"""Put the toolchain and ch32rv where arduino-cli finds them for the working tree.

The bench builds the working tree, not a release: bench/conftest.py links the repository into
the sketchbook (<user>/hardware/ch32-riscv-ug/ch32rv) and the bench profiles name the platform
without a version, so arduino-cli takes that link. A platform installed that way has no tools
of its own; arduino-cli then resolves {runtime.tools.<name>.path} from the tools installed under
<data>/packages/*/tools/<name>/<version>. This script puts the two the platform needs there -
the versions tools_*.json names, the same files fetch_tools.py unpacked into <repo>/.tools - as
symlinks (a copy where the OS refuses one):

    <data>/packages/ch32-riscv-ug/tools/xpack-riscv-none-elf-gcc/14.3.0-1 -> <repo>/.tools/xpack-riscv-none-elf-gcc/14.3.0-1
    <data>/packages/ch32-riscv-ug/tools/ch32rv/0.12.4                     -> <repo>/.tools/ch32rv/0.12.4

    uv run tests/bench/install_tools.py            # link (after fetch_tools.py; again after bump_tool.py)
    uv run tests/bench/install_tools.py --check    # what pytest bench does first

Two things must NOT be there: this platform installed through Board Manager (arduino-cli would
take whichever of the two has the higher version, so a release could silently stand in for the
tree), and a hardware/ link that points somewhere else. --check names both.
"""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(REPO / "tools" / "index"))
import fetch_tools                                   # noqa: E402

PACKAGER = "ch32-riscv-ug"
ARCH = "ch32rv"
TOOLS = ("xpack-riscv-none-elf-gcc", "ch32rv")       # what platform.txt asks for by {runtime.tools.<name>.path}


def _config_dir(key: str, env: str, default: pathlib.Path) -> pathlib.Path:
    """arduino-cli's directory: the environment variable it honours, else its config, else the default."""
    value = os.environ.get(env)
    if not value:
        try:
            value = subprocess.run(["arduino-cli", "config", "get", key], check=True, capture_output=True,
                                   text=True, timeout=30).stdout.strip()
        except (OSError, subprocess.SubprocessError):
            value = ""
    return pathlib.Path(value).expanduser() if value else default


def data_dir() -> pathlib.Path:
    return _config_dir("directories.data", "ARDUINO_DIRECTORIES_DATA", pathlib.Path.home() / ".arduino15")


def user_dir() -> pathlib.Path:
    return _config_dir("directories.user", "ARDUINO_DIRECTORIES_USER", pathlib.Path.home() / "Arduino")


def wanted() -> list[tuple[str, str, pathlib.Path]]:
    """[(name, version, where fetch_tools.py unpacked it)] for the tools the platform needs."""
    frags = fetch_tools.fragments()
    out = []
    for name in TOOLS:
        if name not in frags:
            raise SystemExit(f"tools/index has no tools_*.json for {name}")
        version = frags[name]["version"]
        out.append((name, version, fetch_tools.tool_dir(REPO / ".tools", name, version)))
    return out


def platform_link() -> pathlib.Path:
    return user_dir() / "hardware" / PACKAGER / ARCH


def check(data: pathlib.Path | None = None) -> list[str]:
    """What stands between arduino-cli and the working tree, in words; empty means ready."""
    data = data or data_dir()
    problems = []
    for name, version, src in wanted():
        dest = data / "packages" / PACKAGER / "tools" / name / version
        if not (dest.exists() and any(dest.iterdir())):
            problems.append(f"{name} {version} is not under {data / 'packages' / PACKAGER / 'tools'}")
        elif dest.is_symlink() and dest.resolve() != src.resolve():
            problems.append(f"{dest} points at {dest.resolve()}, not {src}")
    installed = data / "packages" / PACKAGER / "hardware"
    if installed.exists() and any(installed.iterdir()):
        problems.append(f"{installed} holds an installed {PACKAGER}:{ARCH}: it would stand in for the working tree "
                        f"(arduino-cli takes the higher version); remove it: arduino-cli core uninstall {PACKAGER}:{ARCH}")
    link = platform_link()
    if (link.exists() or link.is_symlink()) and not (link.is_symlink() and link.resolve() == REPO.resolve()):
        problems.append(f"{link} exists and is not a link to {REPO}: remove it or point it here")
    return problems


def install(data: pathlib.Path | None = None) -> None:
    data = data or data_dir()
    for name, version, src in wanted():
        if not src.exists():
            raise SystemExit(f"{src} is missing: run uv run tools/index/fetch_tools.py first")
        dest = data / "packages" / PACKAGER / "tools" / name / version
        if dest.is_symlink() and dest.resolve() == src.resolve():
            print(f"kept    {dest}")
            continue
        if dest.is_symlink() or dest.exists():
            raise SystemExit(f"{dest} exists and is not a link to {src}: remove it first")
        dest.parent.mkdir(parents=True, exist_ok=True)
        try:
            dest.symlink_to(src, target_is_directory=True)
            print(f"linked  {dest} -> {src}")
        except OSError:                       # Windows without Developer Mode: a copy instead
            shutil.copytree(src, dest)
            print(f"copied  {src} -> {dest}")
    for p in check(data):
        print(f"still: {p}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true", help="compare only, exit 1 when something is missing")
    ap.add_argument("--data", type=pathlib.Path, help="arduino-cli's data directory (default: its config)")
    args = ap.parse_args()
    if args.check:
        problems = check(args.data)
        for p in problems:
            print(p)
        if problems:
            print(f"run: uv run {pathlib.Path(__file__).relative_to(REPO)}")
        return 1 if problems else 0
    install(args.data)
    return 0


if __name__ == "__main__":
    sys.exit(main())
