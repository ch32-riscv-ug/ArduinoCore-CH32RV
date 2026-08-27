#!/usr/bin/env -S uv run --no-project --script
# /// script
# requires-python = ">=3.10"
# ///
"""Compile a sketch for every part number the generator produced.

W-3/W-7. The broadest cheap signal the project has: every ISA, GPIO width,
vector layout and linker script the family needs, built the way a user builds
(arduino-cli against the platform), and the resulting ELF sizes recorded so a
change in code size has to be acknowledged rather than noticed later.

  uv run tests/compile/compile_matrix.py <workdir>

Normally reached through `pytest` (tests/compile/test_compile_matrix.py); the direct
form is for iterating on a failure.

Runs in symlink mode - the working tree *is* the platform - so it tests
uncommitted changes. The Board Manager path is a separate harness
(tools/index/install_check.py).

Everything happens in sandboxed arduino-cli directories, so the user's real
~/.arduino15 and sketchbook are untouched.
"""
import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(REPO / "tools" / "index"))

from fetch_tools import env_defaults      # noqa: E402

# The repository root is the Arduino platform directory (R-15 method A). These
# are the entries arduino-cli reads, and the same list defines what a release
# archive must contain - keep in step with PLATFORM_ENTRIES in gen_index.py.
PLATFORM_ENTRIES = ("platform.txt", "boards.txt", "programmers.txt", "cores",
                    "variants", "libraries", "bootloaders", "system", "debug")

BLINK = """\
// Global constructor to exercise C++ compilation, and to give the .init_array
// checks below something to find.
struct Marker { int v; Marker() : v(42) {} };
Marker marker;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(500);
  digitalWrite(LED_BUILTIN, LOW);
  delay(500);
}
"""

# ADR-0007: build.extra_flags belongs to the user. If the core ever starts
# consuming it, this sketch stops compiling.
EXTRA_FLAGS = """\
#ifndef CH32_EXTRA_FLAGS_SMOKE
#error "build.extra_flags did not reach the sketch"
#endif
void setup() {}
void loop() {}
"""


class Failure(Exception):
    """A check failed. The message is what the operator needs to see."""


def gcc_bin() -> pathlib.Path:
    """The toolchain, from the environment or <repo>/.tools."""
    path = os.environ.get("CH32_GCC_BIN") or env_defaults(REPO / ".tools").get(
        "CH32_GCC_BIN")
    path = pathlib.Path(path)
    if not (path / "riscv-none-elf-gcc").exists() and not (
            path / "riscv-none-elf-gcc.exe").exists():
        raise Failure(f"no toolchain at {path}; "
                      f"run: uv run tools/index/fetch_tools.py")
    return path


def tool(gcc: pathlib.Path, name: str) -> str:
    return str(gcc / f"riscv-none-elf-{name}")


def sandbox(work: pathlib.Path) -> dict:
    """arduino-cli directories under the work dir, never the user's own."""
    env = dict(os.environ)
    env.update(ARDUINO_DIRECTORIES_USER=str(work / "user"),
               ARDUINO_DIRECTORIES_DATA=str(work / "data"),
               ARDUINO_DIRECTORIES_DOWNLOADS=str(work / "staging"))
    return env


def link_platform(work: pathlib.Path) -> None:
    """Make the working tree visible to arduino-cli as an installed platform.

    A symlink where the OS allows one, a copy where it does not: Windows needs
    a privilege for symlinks that CI does not have.
    """
    dest = work / "user" / "hardware" / "ch32-riscv-ug" / "ch32v"
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.is_symlink() or dest.exists():
        if dest.is_symlink():
            dest.unlink()
        else:
            shutil.rmtree(dest)
    try:
        dest.symlink_to(REPO, target_is_directory=True)
        return
    except (OSError, NotImplementedError):
        pass
    dest.mkdir(parents=True)
    for entry in PLATFORM_ENTRIES:
        src = REPO / entry
        if not src.exists():
            continue
        if src.is_dir():
            shutil.copytree(src, dest / entry)
        else:
            shutil.copy2(src, dest / entry)


def targets() -> list:
    """[(board, part number)] from the generated boards.txt, in file order."""
    text = (REPO / "boards.txt").read_text(encoding="utf-8")
    return re.findall(r"^([A-Za-z0-9_]+)\.menu\.pnum\.([A-Za-z0-9]+)=", text,
                      re.M)


def sketch(work: pathlib.Path, name: str, source: str) -> pathlib.Path:
    """arduino-cli requires the .ino to be named after its directory."""
    d = work / name
    d.mkdir(parents=True, exist_ok=True)
    (d / f"{name}.ino").write_text(source, encoding="utf-8")
    return d


def compile_one(env, fqbn, gcc, build, src, extra_properties=()) -> tuple:
    cmd = ["arduino-cli", "compile", "--fqbn", fqbn,
           "--build-property", f"compiler.path={gcc}{os.sep}",
           "--build-path", str(build), str(src)]
    for prop in extra_properties:
        cmd[4:4] = ["--build-property", prop]
    proc = subprocess.run(cmd, env=env, capture_output=True, text=True)
    return proc.returncode, proc.stdout + proc.stderr


def elf_sizes(gcc: pathlib.Path, elf: pathlib.Path) -> tuple:
    """(text, data, bss) from riscv-none-elf-size's default output."""
    out = subprocess.run([tool(gcc, "size"), str(elf)],
                         capture_output=True, text=True, check=True).stdout
    fields = out.splitlines()[1].split()
    return int(fields[0]), int(fields[1]), int(fields[2])


def section_size(gcc: pathlib.Path, elf: pathlib.Path, section: str) -> int:
    out = subprocess.run([tool(gcc, "size"), "-A", str(elf)],
                         capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0] == section:
            return int(parts[1])
    return 0


def check_static_cxx(gcc: pathlib.Path, elf: pathlib.Path) -> str:
    """The C++ runtime wiring is present in the image.

    Not that it runs - that is hardware's job - but that the pieces exist: the
    sketch's global constructor is registered in .init_array, and crt0 has the
    loop that would call it.
    """
    init_array = section_size(gcc, elf, ".init_array")
    if init_array == 0:
        raise Failure(".init_array is empty: the global constructor was not registered")

    syms = subprocess.run([tool(gcc, "nm"), str(elf)],
                          capture_output=True, text=True, check=True).stdout
    if "_GLOBAL__sub_I" not in syms:
        raise Failure("no _GLOBAL__sub_I symbol: the sketch's constructor was dropped")

    disasm = subprocess.run([tool(gcc, "objdump"), "-d", str(elf)],
                            capture_output=True, text=True, check=True).stdout
    reset = re.search(r"<handle_reset>:\n(.*?)\n\n", disasm, re.S)
    if reset is None or "jalr" not in reset.group(1):
        raise Failure("crt0's handle_reset has no jalr: the .init_array call "
                      "loop is missing")
    return f"INIT_ARRAY CHECKS OK (.init_array={init_array} bytes)"


def run(work: pathlib.Path) -> dict:
    """Compile everything. Returns the results; raises Failure on a hard stop."""
    work.mkdir(parents=True, exist_ok=True)
    gcc = gcc_bin()
    if shutil.which("arduino-cli") is None:
        raise Failure("arduino-cli is not on PATH")
    env = sandbox(work)
    link_platform(work)
    blink = sketch(work, "Blink", BLINK)

    pairs = targets()
    boards = sorted({b for b, _ in pairs})
    print(f"== targets: {len(pairs)} across {len(boards)} boards", flush=True)

    sizes, failures = {}, []
    lines = []
    for board, pnum in pairs:
        key = f"{board}/{pnum}"
        print(f"== compile Blink for {key} ==", flush=True)
        build = work / f"build-{board}-{pnum}"
        rc, output = compile_one(env, f"ch32-riscv-ug:ch32v:{board}:pnum={pnum}",
                                 gcc, build, blink)
        if rc != 0:
            failures.append((key, output.strip().splitlines()[-15:]))
            continue
        text, data, bss = elf_sizes(gcc, build / "Blink.ino.elf")
        sizes[key] = {"text": text, "data": data, "bss": bss}
        line = f"{key}\t{text}\t{data}\t{bss}"
        lines.append(line)
        print(line, flush=True)

    (work / "sizes.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8")

    if failures:
        detail = "\n".join(f"  {k}\n    " + "\n    ".join(t) for k, t in failures)
        raise Failure(f"{len(failures)} of {len(pairs)} part numbers failed to "
                      f"compile:\n{detail}")

    # Static checks on one representative build.
    print(check_static_cxx(gcc, work / "build-CH32V006-ANY" / "Blink.ino.elf"),
          flush=True)

    extra = sketch(work, "ExtraFlags", EXTRA_FLAGS)
    rc, output = compile_one(
        env, "ch32-riscv-ug:ch32v:CH32V006:pnum=ANY", gcc,
        work / "build-extraflags", extra,
        ["build.extra_flags=-DCH32_EXTRA_FLAGS_SMOKE=1"])
    if rc != 0:
        raise Failure("build.extra_flags did not reach the sketch (ADR-0007):\n"
                      + output[-2000:])
    print("EXTRA_FLAGS INJECTION OK", flush=True)

    return {"sizes": sizes, "boards": boards, "targets": len(pairs)}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("workdir", type=pathlib.Path)
    args = ap.parse_args()
    try:
        run(args.workdir)
    except Failure as e:
        print(f"FAIL: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
