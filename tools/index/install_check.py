#!/usr/bin/env -S uv run --no-project --script
# /// script
# requires-python = ">=3.10"
# ///
"""Install this platform the way a user does, then build with no overrides.

W-5. Compiling the working tree proves nothing about the release archive: it
can reference a file the archive does not ship, or lean on a path override that
only exists during development. So this generates the package index, serves it
over loopback, installs into an empty arduino-cli data directory, and compiles
with no `--build-property` at all - the toolchain has to come from the tool the
index pulled down.

  uv run tools/index/install_check.py <workdir>

Normally reached through `pytest` (tests/test_package_install.py).

The xPack archive is served from <repo>/.tools/cache rather than fetched from
GitHub on every run (400 MB). CH32_PROBE_RS_ARCHIVE does the same for probe-rs;
without it probe-rs is downloaded from the mirror, which is what CI does.

On Windows the work directory has to be shallow - see check_path_budget.
"""
import argparse
import contextlib
import functools
import hashlib
import http.server
import json
import os
import pathlib
import re
import shutil
import socketserver
import subprocess
import sys
import threading

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(REPO / "tests" / "compile"))

import gen_index                                    # noqa: E402
from compile_matrix import Failure                  # noqa: E402
from fetch_tools import env_defaults                # noqa: E402

PACKAGER = "ch32-riscv-ug"
ARCH = "ch32v"
FQBN_BLINK = f"{PACKAGER}:{ARCH}:CH32V006:pnum=ANY"
FQBN_ACCEPTANCE = f"{PACKAGER}:{ARCH}:CH32X035:pnum=ANY"
FQBN_LIBRARIES = f"{PACKAGER}:{ARCH}:CH32X035:pnum=ANY"

# Nothing of the repository's own scaffolding may reach a user's machine.
# gen_index.py packages an allowlist, so a leak means the allowlist grew
# something it should not have.
FORBIDDEN = ("tests", "docs", "tools", "vendor", ".git", ".github")

# Measured, not guessed: `riscv-none-elf-g++ -E -v` prints this include
# directory verbatim, unresolved dot-dots and all, and GCC opens it that way -
# canonicalising only for diagnostics. On Windows that makes the depth of the
# sandbox a correctness question; see the note on _short_root in conftest.py.
GCC_HEADER_TAIL = ("/bin/../lib/gcc/riscv-none-elf/{v}/../../../../riscv-none-elf"
                   "/include/c++/{v}/riscv-none-elf/rv32ec/ilp32e/bits/c++config.h")
MAX_PATH = 259


BLINK = """\
void setup() { pinMode(LED_BUILTIN, OUTPUT); }
void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(500);
  digitalWrite(LED_BUILTIN, LOW);
  delay(500);
}
"""


# The bundled libraries only exist for a user if the release archive carries
# libraries/ and arduino-cli finds them there without a sketchbook copy. The
# working tree cannot show that: it is the archive's allowlist that decides.
LIBRARIES = """\
#include <SPI.h>
#include <Wire.h>
void setup() {
  Wire.begin();
  SPI.begin();
}
void loop() {}
"""


def check_path_budget(work: pathlib.Path) -> None:
    """Refuse too deep a sandbox now, while the reason is still legible.

    Past the limit this fails as "bits/c++config.h: No such file or directory",
    naming a path that is short and plainly present - so the hunt starts in the
    wrong place. One check up front costs nothing and says what is wrong.
    """
    if os.name != "nt":
        return
    frag = json.loads((HERE / "tools_xpack_gcc.json").read_text(encoding="utf-8"))
    root = work / "data" / "packages" / PACKAGER / "tools" / frag["name"] / frag["version"]
    longest = len(str(root)) + len(GCC_HEADER_TAIL.format(v=frag["version"].split("-")[0]))
    if longest > MAX_PATH:
        raise Failure(
            f"this sandbox is {longest - MAX_PATH} characters too deep for "
            f"Windows: the toolchain installs under {root}, and GCC would then "
            f"have to open a {longest}-character path to reach its own headers. "
            f"Point CH32_TEST_TMP at a shorter directory.")


@contextlib.contextmanager
def serving(directory: pathlib.Path, port: int):
    """Serve `directory` on loopback for the duration of the block.

    A context manager rather than a background process and a trap: the shell
    version leaked a listening server whenever it was killed before its EXIT
    trap ran, and needed a `sleep 1` in the hope the server was up. Here the
    socket is already bound when the block starts.
    """
    class Handler(http.server.SimpleHTTPRequestHandler):
        def log_message(self, fmt, *args):
            pass          # one line per request drowns the actual output

    handler = functools.partial(Handler, directory=str(directory))

    class Quiet(socketserver.TCPServer):
        allow_reuse_address = True

        def handle_error(self, request, client_address):
            pass          # a client hanging up is not this test's problem

    with Quiet(("127.0.0.1", port), handler) as httpd:
        thread = threading.Thread(target=httpd.serve_forever, daemon=True)
        thread.start()
        try:
            yield f"http://127.0.0.1:{port}"
        finally:
            httpd.shutdown()
            thread.join(timeout=5)


def sha256(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def stage_archive(archive: pathlib.Path, fragment: pathlib.Path,
                  www: pathlib.Path) -> str:
    """Copy a tool archive under the name the index expects for it.

    Matched by checksum, so a wrong or truncated archive fails here rather than
    as a confusing install error later.
    """
    want = "SHA-256:" + sha256(archive)
    frag = json.loads(fragment.read_text(encoding="utf-8"))
    for system in frag["systems"]:
        if system["checksum"] == want:
            name = system["archiveFileName"]
            shutil.copy(archive, www / name)
            return name
    raise Failure(f"{archive.name} matches no checksum in {fragment.name}; "
                  f"it is not the archive this index describes")


def cli(*args, env, check=True, capture=True):
    proc = subprocess.run(["arduino-cli", *args], env=env, text=True,
                          capture_output=capture)
    if check and proc.returncode != 0:
        raise Failure("arduino-cli " + " ".join(args[:2]) + " failed:\n"
                      + (proc.stdout or "") + (proc.stderr or ""))
    return (proc.stdout or "") + (proc.stderr or "")


def sketch(work: pathlib.Path, name: str, source=None, copy_from=None):
    d = work / name
    d.mkdir(parents=True, exist_ok=True)
    target = d / f"{name}.ino"
    if copy_from is not None:
        shutil.copy(copy_from, target)
    else:
        target.write_text(source, encoding="utf-8")
    return d


def platform_version(platform_txt: pathlib.Path) -> str:
    m = re.search(r"^version=(.*)$", platform_txt.read_text(encoding="utf-8"),
                  re.M)
    return m.group(1).strip()


def bump_patch(version: str) -> str:
    major, minor, patch = version.split(".")
    return f"{major}.{minor}.{int(patch) + 1}"


def stage_platform_copy(dest: pathlib.Path) -> pathlib.Path:
    """A copy of just the release entries, for generating a second version.

    Only what gen_index.py reads: copying the repository root would also copy
    the work directory when it happens to live inside it.
    """
    dest.mkdir(parents=True, exist_ok=True)
    for entry in gen_index.PLATFORM_ENTRIES:
        src = REPO / entry
        if not src.exists():
            continue
        if src.is_dir():
            shutil.copytree(src, dest / entry, dirs_exist_ok=True)
        else:
            shutil.copy2(src, dest / entry)
    return dest


def run(work: pathlib.Path, port: int = 8731) -> dict:
    work.mkdir(parents=True, exist_ok=True)
    check_path_budget(work)
    www = work / "www"
    www.mkdir(exist_ok=True)
    if shutil.which("arduino-cli") is None:
        raise Failure("arduino-cli is not on PATH")

    defaults = env_defaults(REPO / ".tools")
    xpack = pathlib.Path(os.environ.get("CH32_XPACK_ARCHIVE")
                         or defaults["CH32_XPACK_ARCHIVE"])
    if not xpack.exists():
        raise Failure(f"no xPack archive at {xpack}; "
                      f"run: uv run tools/index/fetch_tools.py")
    probe_archive = os.environ.get("CH32_PROBE_RS_ARCHIVE")

    local_tools = ["xpack-riscv-none-elf-gcc"]
    if probe_archive:
        local_tools.append("probe-rs")

    base_url = f"http://127.0.0.1:{port}"
    with serving(www, port) as base_url:
        # 1) package the platform and generate the index
        gen_index.main_with_args([
            "--platform", str(REPO), "--out", str(www),
            "--base-url", base_url, "--tools", "local",
            "--local-tools", ",".join(local_tools)])
        stage_archive(xpack, HERE / "tools_xpack_gcc.json", www)
        if probe_archive:
            stage_archive(pathlib.Path(probe_archive),
                          HERE / "tools_probe_rs.json", www)

        # 2) clean install into sandboxed directories - a fresh data directory
        #    is what makes this the real user path rather than a warm cache.
        env = dict(os.environ,
                   ARDUINO_DIRECTORIES_USER=str(work / "user"),
                   ARDUINO_DIRECTORIES_DATA=str(work / "data"),
                   ARDUINO_DIRECTORIES_DOWNLOADS=str(work / "staging"))
        index_url = f"{base_url}/package_{PACKAGER}_index.json"
        cli("core", "update-index", "--additional-urls", index_url, env=env)
        cli("core", "install", f"{PACKAGER}:{ARCH}",
            "--additional-urls", index_url, env=env)

        # 3) compile with no overrides at all
        sizes = {}
        for name, fqbn, src in (
                ("Blink", FQBN_BLINK, sketch(work, "Blink", BLINK)),
                ("Acceptance", FQBN_ACCEPTANCE,
                 sketch(work, "Acceptance", copy_from=REPO / "tests" / "sketches"
                        / "basic" / "serial_println" / "serial_println.ino")),
                ("Libraries", FQBN_LIBRARIES,
                 sketch(work, "Libraries", LIBRARIES)),
        ):
            out = cli("compile", "--fqbn", fqbn, "--build-path",
                      str(work / f"build-{name}"), str(src), env=env)
            m = re.search(r"Sketch uses (\d+) bytes", out)
            sizes[name] = int(m.group(1)) if m else None
            print(f"{name}: {out.strip().splitlines()[0] if out.strip() else 'built'}",
                  flush=True)

        installed = work / "data" / "packages" / PACKAGER / "hardware" / ARCH
        leaked = [p for p in installed.glob("*/*") if p.name in FORBIDDEN]
        if leaked:
            raise Failure(f"release archive contains {leaked[0]}")
        print("ARCHIVE CONTENTS OK", flush=True)

        # 4) the upload path is only real if the programmer came down with the
        #    platform. probe-rs is where Windows install used to break.
        tools_dir = work / "data" / "packages" / PACKAGER / "tools" / "probe-rs"
        probes = [p for p in tools_dir.glob("*/probe-rs*") if p.is_file()]
        if not probes:
            raise Failure("probe-rs was not installed with the platform")
        proc = subprocess.run([str(probes[0]), "--version"], capture_output=True,
                              text=True)
        if proc.returncode != 0:
            raise Failure(f"installed probe-rs does not run: {proc.stderr}")
        print(f"PROBE-RS INSTALL OK: {probes[0]}", flush=True)

        # 5) upgrade, then back. A Board Manager index is append-only: users
        #    upgrade in place, and regenerating the index from scratch used to
        #    drop every older version - invisible until someone pins one.
        current = platform_version(REPO / "platform.txt")
        nxt = bump_patch(current)
        staged = stage_platform_copy(work / "upgrade" / "platform")
        ptxt = staged / "platform.txt"
        ptxt.write_text(re.sub(r"^version=.*$", f"version={nxt}",
                               ptxt.read_text(encoding="utf-8"), count=1,
                               flags=re.M), encoding="utf-8")
        gen_index.main_with_args([
            "--platform", str(staged), "--out", str(www),
            "--base-url", base_url, "--tools", "local",
            "--local-tools", ",".join(local_tools),
            "--merge", str(www / f"package_{PACKAGER}_index.json")])

        cli("core", "update-index", "--additional-urls", index_url, env=env)
        cli("core", "upgrade", f"{PACKAGER}:{ARCH}",
            "--additional-urls", index_url, env=env)
        if nxt not in cli("core", "list", env=env):
            raise Failure(f"upgrade to {nxt} did not take")
        cli("core", "install", f"{PACKAGER}:{ARCH}@{current}",
            "--additional-urls", index_url, env=env)
        if current not in cli("core", "list", env=env):
            raise Failure(f"index is not append-only: {current} is gone")
        print(f"UPGRADE AND ROLLBACK OK ({nxt} then back to {current})",
              flush=True)

    print("INSTALL-AND-COMPILE OK", flush=True)
    return {"sizes": sizes, "probe_rs": str(probes[0]),
            "versions": [current, nxt]}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("workdir", type=pathlib.Path)
    ap.add_argument("--port", type=int, default=int(os.environ.get("PORT", 8731)))
    args = ap.parse_args()
    try:
        run(args.workdir, args.port)
    except Failure as e:
        print(f"FAIL: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
