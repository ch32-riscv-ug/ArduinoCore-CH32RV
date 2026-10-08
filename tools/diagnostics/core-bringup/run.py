#!/usr/bin/env python3
"""Explicit UART-free core bring-up; default is compile only."""
import argparse
import fcntl
import hashlib
import json
import os
import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parents[3]
GEOMETRY = {
    "CH32V205RCT6": (262144, 262144, bytes.fromhex("ffffffff")),
    "CH32X315MCU6": (491520, 196608, bytes.fromhex("39e339e3")),
}


def restore_prefix(data, writable, blank):
    if len(data) % len(blank):
        raise ValueError("Backup length is not a whole erased word")
    last = 0
    for offset in range(0, len(data), len(blank)):
        if data[offset:offset + len(blank)] != blank:
            last = offset + len(blank)
    size = max(256, (last + 255) // 256 * 256)
    if size > writable:
        raise ValueError("Nonblank bytes beyond writable region: cannot safely restore")
    return data[:size]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chip", choices=GEOMETRY, required=True)
    parser.add_argument("--probe", help="Explicit serial: selector; no default")
    parser.add_argument("--out", type=pathlib.Path, required=True)
    parser.add_argument("--execute", action="store_true",
                        help="Back up, flash, test, then restore and compare all flash")
    args = parser.parse_args()
    if args.execute and (not args.probe or not args.probe.startswith("serial:")):
        parser.error("--execute requires an explicit serial: probe selector")
    work = args.out.resolve()
    work.mkdir(parents=True, exist_ok=False)
    metadata = json.loads((REPO / "tools/index/tools_ch32rv.json").read_text())
    tool = REPO / ".tools/ch32rv" / metadata["version"] / "ch32rv"
    gcc_version = json.loads((REPO / "tools/index/tools_xpack_gcc.json").read_text())["version"]
    gcc = REPO / ".tools/xpack-riscv-none-elf-gcc" / gcc_version / "bin"
    env = dict(os.environ, ARDUINO_DIRECTORIES_USER=str(work / "user"),
               ARDUINO_DIRECTORIES_DATA=str(work / "data"),
               ARDUINO_DIRECTORIES_DOWNLOADS=str(work / "downloads"))
    platform = work / "user/hardware/ch32-riscv-ug/ch32rv"
    platform.parent.mkdir(parents=True)
    platform.symlink_to(REPO, target_is_directory=True)
    board = args.chip[:8]
    fqbn = f"ch32-riscv-ug:ch32rv:{board}:pnum={args.chip}"
    command = ["arduino-cli", "compile", "--fqbn", fqbn,
               "--build-property", f"compiler.path={gcc}{os.sep}",
               "--build-path", str(work / "build"), str(pathlib.Path(__file__).parent / "core_minimal")]
    build = subprocess.run(command, env=env, capture_output=True, text=True, timeout=120)
    (work / "compile.log").write_text(build.stdout + build.stderr)
    if build.returncode:
        raise RuntimeError("Compile failed; see compile.log")
    result = {"chip": args.chip, "fqbn": fqbn, "tool": metadata["version"],
              "gcc": gcc_version,
              "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=REPO, text=True).strip(),
              "dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=REPO)),
              "compiled": True, "executed": False}
    if not args.execute:
        (work / "result.json").write_text(json.dumps(result, indent=2))
        print("COMPILE PASS", work)
        return
    lock_name = hashlib.sha256(args.probe.encode()).hexdigest()
    with open(f"/tmp/ch32-core-bringup-{lock_name}.lock", "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        index = 0
        def invoke(name, arguments, *, machine=True, input=None):
            nonlocal index
            index += 1
            cmd = [str(tool), "--probe", args.probe, "--chip", args.chip,
                   "--non-interactive"]
            if machine:
                cmd.append("--json")
            cmd += arguments
            proc = subprocess.run(cmd, capture_output=True, input=input, timeout=120)
            stem = work / f"{index:02d}-{name}"
            stem.with_suffix(".stdout").write_bytes(proc.stdout)
            stem.with_suffix(".stderr").write_bytes(proc.stderr)
            stem.with_suffix(".command.json").write_text(json.dumps(cmd))
            if proc.returncode:
                raise RuntimeError(f"{name}: tool exit {proc.returncode}; see {stem}")
            return json.loads(proc.stdout) if machine else proc.stdout.decode(errors="replace")
        info = invoke("info-before", ["target", "info"])
        if info["target"]["sku"] != args.chip or not info["target"]["verified"]:
            raise RuntimeError("Target identity does not match a verified SKU")
        option = invoke("option-before", ["target", "option", "get"])["result"]
        if option["read_protected"] or option["write_protected"]:
            raise RuntimeError("Protected target: refusing bring-up")
        total, writable, blank = GEOMETRY[args.chip]
        if info["target"]["flash_bytes"] != writable:
            raise RuntimeError("DB programming capacity differs from restore contract")
        backup = work / "flash-before.bin"
        invoke("backup", ["read", "--range", f"0x08000000+{total}", "-o", str(backup)])
        original = backup.read_bytes()
        if len(original) != total:
            raise RuntimeError("Incomplete backup")
        prefix = work / "restore-prefix.bin"
        prefix.write_bytes(restore_prefix(original, writable, blank))
        result.update(probe=args.probe, uid=info["target"]["uid"],
                      backup_sha256=hashlib.sha256(original).hexdigest(),
                      backup_bytes=total, writable_bytes=writable,
                      restored=False, executed=True)
        # Persist recovery inputs before any destructive command.
        (work / "result.json").write_text(json.dumps(result, indent=2))
        try:
            elf = work / "build/core_minimal.ino.elf"
            output = invoke("run", ["run", str(elf), "--source", "dmseq",
                                  "--duration", "6"], machine=False, input=b"R")
            result["passed"] = "BRINGUP PASS" in output and "BRINGUP FAIL" not in output
            result["output"] = output
            if not result["passed"]:
                raise RuntimeError("Core bring-up failed; see run transcript")
        finally:
            try:
                invoke("restore", ["flash", str(prefix), "--erase", "chip", "--confirm-run", "pc"])
                restored = work / "flash-after.bin"
                invoke("read-restored", ["read", "--range", f"0x08000000+{total}", "-o", str(restored)])
                actual = restored.read_bytes()
                result["restored_sha256"] = hashlib.sha256(actual).hexdigest()
                option_after = invoke("option-after", ["target", "option", "get"])["result"]
                info_after = invoke("info-after", ["target", "info"])["target"]
                result["restored"] = (actual == original and option_after["raw"] == option["raw"]
                                      and info_after["uid"] == result["uid"])
                if not result["restored"]:
                    raise RuntimeError("RESTORE FAILED: preserve backup and recover before further tests")
            finally:
                (work / "result.json").write_text(json.dumps(result, indent=2))
        print("BRINGUP PASS; FLASH/OPTION RESTORED", work)


if __name__ == "__main__":
    main()
