#!/usr/bin/env -S uv run --no-project --script
# /// script
# requires-python = ">=3.10"
# ///
"""Every sketch profile builds the way the documentation says it does.

A user who follows tests/README.ja.md runs `arduino-cli compile --profile
<name>`, and arduino-cli then resolves the platform *through the package index*
named in sketch.yaml - not through an installed core and not through --fqbn.
Nothing covered that path:

  test_sketch_profiles.py   --fqbn against the working tree; it deliberately
                            copies the .ino out from under its sketch.yaml
  test_package_install.py   installs from the index, then builds with --fqbn
  this                      the index *and* the profile, which is what the
                            instructions actually tell people to type

  uv run tests/sketches/profile_build.py <workdir>

Normally reached through `pytest` (tests/test_sketch_profile_build.py).

The index the profiles name is not published yet, so one is generated and
served on loopback, and the sketches are built from copies whose
platform_index_url points at it. Copies rather than the tree itself: rewriting
a committed generated file for the duration of a test leaves the repository
dirty if the run is interrupted, and this has to be safe to Ctrl-C.
"""
import argparse
import os
import pathlib
import re
import shutil
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(REPO / "tools" / "index"))
sys.path.insert(0, str(REPO / "tests" / "compile"))

import gen_index                                            # noqa: E402
import install_check                                        # noqa: E402
from compile_matrix import Failure                          # noqa: E402
from sync_profiles import INDEX_URL                         # noqa: E402

PROFILE = re.compile(r"^  ([a-z0-9_]+):$", re.M)
USED = re.compile(r"Sketch uses (\d+) bytes")


def combinations():
    """[(sketch dir, profile name)] for every profile every sketch.yaml names."""
    out = []
    for yaml in sorted(HERE.glob("*/*/sketch.yaml")):
        for name in PROFILE.findall(yaml.read_text(encoding="utf-8")):
            out.append((yaml.parent, name))
    return out


def stage(src: pathlib.Path, dest: pathlib.Path, base_url: str) -> pathlib.Path:
    """A copy of one sketch whose profiles point at the local index."""
    dest.mkdir(parents=True, exist_ok=True)
    shutil.copy(src / f"{src.name}.ino", dest)
    yaml = (src / "sketch.yaml").read_text(encoding="utf-8")
    local = f"{base_url}/package_{install_check.PACKAGER}_index.json"
    if INDEX_URL not in yaml:
        raise Failure(f"{src.name}/sketch.yaml names no {INDEX_URL}; "
                      f"sync_profiles.py and this test disagree on the index")
    (dest / "sketch.yaml").write_text(yaml.replace(INDEX_URL, local),
                                      encoding="utf-8")
    return dest


def run(work: pathlib.Path, port: int = 8751) -> dict:
    work.mkdir(parents=True, exist_ok=True)
    install_check.check_path_budget(work)
    www = work / "www"
    www.mkdir(exist_ok=True)
    if shutil.which("arduino-cli") is None:
        raise Failure("arduino-cli is not on PATH")

    from fetch_tools import env_defaults
    defaults = env_defaults(REPO / ".tools")
    xpack = pathlib.Path(os.environ.get("CH32_XPACK_ARCHIVE")
                         or defaults["CH32_XPACK_ARCHIVE"])
    if not xpack.exists():
        raise Failure(f"no xPack archive at {xpack}; "
                      f"run: uv run tools/index/fetch_tools.py")
    probe_archive = os.environ.get("CH32_PROBE_RS_ARCHIVE")
    local_tools = ["xpack-riscv-none-elf-gcc"] + (["probe-rs"] if probe_archive
                                                  else [])

    with install_check.serving(www, port) as base_url:
        gen_index.main_with_args([
            "--platform", str(REPO), "--out", str(www),
            "--base-url", base_url, "--tools", "local",
            "--local-tools", ",".join(local_tools)])
        install_check.stage_archive(xpack, REPO / "tools" / "index"
                                    / "tools_xpack_gcc.json", www)
        if probe_archive:
            install_check.stage_archive(
                pathlib.Path(probe_archive),
                REPO / "tools" / "index" / "tools_probe_rs.json", www)

        # A data directory of its own, so what the profile pulls down is what
        # gets built - not something a previous run left installed.
        env = dict(os.environ,
                   ARDUINO_DIRECTORIES_USER=str(work / "user"),
                   ARDUINO_DIRECTORIES_DATA=str(work / "data"),
                   ARDUINO_DIRECTORIES_DOWNLOADS=str(work / "staging"))

        results, failures = {}, []
        for src, profile in combinations():
            # One directory per profile, and the sketch keeps its own name
            # inside it: arduino-cli requires the main .ino to match the
            # directory it sits in.
            staged = stage(src, work / "sketches" / profile / src.name,
                           base_url)
            build = work / "build"
            if build.exists():
                shutil.rmtree(build)
            out = install_check.cli("compile", "--profile", profile,
                                    "--build-path", str(build), str(staged),
                                    env=env, check=False)
            used = USED.search(out)
            ok = used is not None
            print(f"== {src.name:16} {profile:10} "
                  + (used.group(0) if ok else "FAIL"), flush=True)
            if ok:
                results[(src.name, profile)] = int(used.group(1))
            else:
                failures.append((src.name, profile,
                                 out.strip().splitlines()[-12:]))

    total = len(results) + len(failures)
    if failures:
        detail = "\n".join(f"  {n} / {p}\n    " + "\n    ".join(t)
                           for n, p, t in failures)
        raise Failure(f"SKETCH PROFILE BUILD FAILED: {len(failures)} of "
                      f"{total}\n{detail}")
    print(f"SKETCH PROFILE BUILD OK: {total} combinations", flush=True)
    return results


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("workdir", type=pathlib.Path)
    ap.add_argument("--port", type=int, default=8751)
    args = ap.parse_args()
    try:
        run(args.workdir, args.port)
    except Failure as e:
        print(f"FAIL: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
