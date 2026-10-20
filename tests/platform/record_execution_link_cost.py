#!/usr/bin/env python3
"""Record and independently audit two runs of the fixed link-only probe.

Manual EXP-00 entry; importing this module does not build or access hardware.
This is specific to probe_execution_link_cost.py, not a general benchmark runner.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys


REPO = Path(__file__).resolve().parents[2]
PROBE = Path(__file__).with_name("probe_execution_link_cost.py")
CASES = (
    "baseline", "disabled_with_unused_object", "disabled_with_unused_archive",
    "weak_undefined", "weak_archive_only", "weak_empty_definition",
    "weak_active_object", "strong_active_archive", "constructor_retained_object",
)
ARCHES = {"rv32e": ("rv32ec_zicsr", "ilp32e"),
          "rv32i": ("rv32imac_zicsr", "ilp32")}
INPUTS = (
    "CORE_REDESIGN_PLAN.ja.md", "planning/core-redesign/experiment-spec.ja.md",
    "planning/core-redesign/workloads.ja.md",
    "planning/core-redesign/experiment-tasks.ja.md",
    "planning/core-redesign/tasks/EXP-00/brief.ja.md",
    "tests/platform/probe_execution_link_cost.py",
    "tests/platform/record_execution_link_cost.py", "tools/index/tools_xpack_gcc.json",
)


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def parse_sections(text):
    sections = {}
    for line in text.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[0].startswith("."):
            require(fields[0] not in sections, f"duplicate section {fields[0]}")
            sections[fields[0]] = int(fields[1])
    require(".text" in sections, "section list contains no .text")
    return sections


def parse_symbols(text):
    # nm -S --size-sort: only defined symbols with a size appear here.
    symbols = {}
    for line in text.splitlines():
        fields = line.split()
        if len(fields) == 4:
            address, size, kind, name = fields
            symbols[name] = [int(address, 16), int(size, 16), kind]
    return symbols


def validate_row(row, sections, binary_size, symbols, baseline):
    """Validate report numbers against separate size/nm/objcopy observations."""
    arch, case = row["arch"], row["case"]
    require((row["march"], row["abi"]) == ARCHES[arch], f"ABI changed: {arch}")
    require(row["sections"] == sections, f"sections disagree: {arch}/{case}")
    require(row["flash_bytes"] == binary_size, f"binary size disagrees: {case}")
    flash_sections = sum(sections.get(s, 0) for s in (".text", ".init_array", ".data"))
    ram = sum(sections.get(s, 0) for s in (".data", ".bss"))
    require(row["flash_section_bytes"] == flash_sections, f"Flash sum disagrees: {case}")
    require(row["static_ram_bytes"] == ram, f"RAM sum disagrees: {case}")
    require(row["delta_flash"] == binary_size - baseline[0], f"Flash delta: {case}")
    require(row["delta_ram"] == ram - baseline[1], f"RAM delta: {case}")
    require("user_runs" in symbols and "user_step" in symbols, f"missing user: {case}")
    active = case in ("weak_active_object", "strong_active_archive", "constructor_retained_object")
    hook = active or case == "weak_empty_definition"
    for name, expected in (("service_hook", hook), ("service_buffer", active),
                           ("service_runs", active),
                           ("registered_callback", case == "constructor_retained_object")):
        require((name in symbols) == expected, f"unexpected reachability {case}: {name}")
    if active:
        require(symbols["service_buffer"][1] == 64, f"service buffer size: {case}")
    require(bool(sections.get(".init_array", 0)) == (case == "constructor_retained_object"),
            f"constructor table: {case}")
    if case.startswith("disabled_with_unused"):
        require((binary_size, ram) == baseline, f"unused cost is nonzero: {case}")


class Recorder:
    def __init__(self, out):
        self.out = out
        self.commands = []

    def run(self, command, *, combined=False):
        command = [str(part) for part in command]
        index = len(self.commands)
        entry = {"command": command, "returncode": None,
                 "stdout": f"logs/{index:04}.stdout", "stderr": f"logs/{index:04}.stderr"}
        self.commands.append(entry)
        write_json(self.out / "commands.json", self.commands)
        try:
            result = subprocess.run(command, cwd=self.out, input="", text=True, capture_output=True,
                                    env={**os.environ, "LC_ALL": "C"})
        except OSError as error:
            entry["error"] = str(error)
            (self.out / entry["stdout"]).write_text("")
            (self.out / entry["stderr"]).write_text(str(error))
            write_json(self.out / "commands.json", self.commands)
            raise
        (self.out / entry["stdout"]).write_text(result.stdout)
        (self.out / entry["stderr"]).write_text(result.stderr)
        entry["returncode"] = result.returncode
        write_json(self.out / "commands.json", self.commands)
        require(result.returncode == 0, f"command {index} failed ({result.returncode}): {command}")
        return result.stdout + result.stderr if combined else result.stdout


def snapshot(recorder):
    out = recorder.out
    inputs = {}
    for relative in INPUTS:
        source = REPO / relative
        destination = out / "inputs" / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        inputs[relative] = digest(source)
    git = ["git", "-C", REPO]
    (out / "input.patch").write_text(recorder.run([*git, "diff", "--binary", "HEAD"]))
    return {"head": recorder.run([*git, "rev-parse", "HEAD"]).strip(),
            "status": recorder.run([*git, "status", "--short"]), "inputs": inputs,
            "untracked": recorder.run([*git, "ls-files", "--others", "--exclude-standard"]).splitlines()}


def toolchain(recorder, compiler):
    require(compiler.name.endswith("g++") and compiler.is_file(), "compiler does not exist or is not g++")
    prefix = str(compiler)[:-3]
    tools = {"driver": compiler}
    for name in ("cc1plus", "as", "ld", "collect2"):
        tools[name] = Path(recorder.run([compiler, f"-print-prog-name={name}"]).strip()).resolve()
    for name in ("gcc-ar", "ar", "size", "objcopy", "nm", "objdump"):
        tools[name] = Path(prefix + name).resolve()
    metadata = {}
    for name, path in tools.items():
        require(path.is_file(), f"tool not found: {path}")
        # GCC's internal helpers are not standalone --version clients: collect2
        # can invoke a linker and cc1plus can emit assembly. Identify by hash.
        internal = name in ("cc1plus", "collect2")
        version = [] if internal else recorder.run([path, "--version"], combined=True).splitlines()
        metadata[name] = {"path": str(path), "sha256": digest(path),
                          "version": version[0] if version else None,
                          "version_status": "internal helper; identified by hash" if internal else "observed"}
    libraries = {}
    for arch, (march, abi) in ARCHES.items():
        path = Path(recorder.run([compiler, f"-march={march}", f"-mabi={abi}",
                                  "-print-libgcc-file-name"]).strip()).resolve()
        libraries[arch] = {"path": str(path), "sha256": digest(path),
                           "linked": False, "reason": "probe links with -nostdlib"}
    lock = json.loads((REPO / "tools/index/tools_xpack_gcc.json").read_text())
    systems = {("Linux", "x86_64"): "x86_64-pc-linux-gnu",
               ("Linux", "aarch64"): "aarch64-linux-gnu"}
    host = systems.get((platform.system(), platform.machine()))
    archive = next((s for s in lock["systems"] if s["host"] == host), None)
    require(archive is not None, "EXP-00 recorder requires a supported Linux host")
    cached = REPO / ".tools/cache" / archive["archiveFileName"]
    archive = {**archive, "observed_sha256": digest(cached) if cached.is_file() else None,
               "status": "present" if cached.is_file() else "unavailable"}
    if cached.is_file():
        require(archive["checksum"].split(":", 1)[1].lower() == archive["observed_sha256"],
                "cached tool archive checksum mismatch")
    return {"tools": metadata, "libgcc": libraries, "archive": archive}


def audit(recorder, run, compiler):
    report = json.loads((run / "result.json").read_text())
    require(report["script_sha256"] == digest(PROBE), "probe source changed during run")
    require(report["compiler_sha256"] == digest(compiler), "compiler changed during run")
    require(report["lto"] is False, "LTO condition changed")
    rows = report["results"]
    keys = [(r["arch"], r["case"]) for r in rows]
    require(len(keys) == 18 and set(keys) == {(a, c) for a in ARCHES for c in CASES},
            "missing, duplicate, or unexpected cases")
    observations = {}
    prefix = str(compiler)[:-3]
    for row in rows:
        arch, case = row["arch"], row["case"]
        stem = run / arch / case
        for suffix in ("elf", "map", "bin", "sections.txt", "symbols.txt", "disasm.txt"):
            require(Path(str(stem) + "." + suffix).is_file(), f"missing artifact {stem}.{suffix}")
        elf = Path(str(stem) + ".elf")
        sections_text = recorder.run([prefix + "size", "-A", elf])
        sections = parse_sections(sections_text)
        require(sections == parse_sections(Path(str(stem) + ".sections.txt").read_text()),
                f"saved section listing differs: {stem}")
        symbol_text = recorder.run([prefix + "nm", "-S", "--size-sort", elf])
        symbols = parse_symbols(symbol_text)
        require(symbol_text == Path(str(stem) + ".symbols.txt").read_text(), f"saved nm differs: {stem}")
        require(recorder.run([prefix + "objdump", "-dr", elf]) ==
                Path(str(stem) + ".disasm.txt").read_text(), f"saved disassembly differs: {stem}")
        # Separate objcopy invocation checks both content and the reported binary length.
        fresh = run / arch / f"{case}.audit.bin"
        recorder.run([prefix + "objcopy", "-O", "binary", elf, fresh])
        require(digest(fresh) == digest(Path(str(stem) + ".bin")), f"binary differs: {stem}")
        baseline = next(r for r in rows if r["arch"] == arch and r["case"] == "baseline")
        validate_row(row, sections, fresh.stat().st_size, symbols,
                     (baseline["flash_bytes"], baseline["static_ram_bytes"]))
        map_text = Path(str(stem) + ".map").read_text()
        # The archive-extraction preamble precedes the discarded-input sections.
        extracted = "service.a(service.o)" in map_text.split("Discarded input sections")[0]
        require(extracted == (case == "strong_active_archive"), f"archive extraction differs: {stem}")
        observations[f"{arch}/{case}"] = {
            "symbols": symbols, "elf_sha256": digest(elf), "bin_sha256": digest(fresh),
            "map_sha256_normalized": hashlib.sha256(map_text.replace(str(run), "<RUN>").encode()).hexdigest(),
            "archive_member_extracted": extracted,
        }
    return {"rows": rows, "observations": observations}


def compare(first, second):
    require(first["rows"] == second["rows"], "run metrics differ")
    require(first["observations"] == second["observations"],
            "symbols, ELF, binary, normalized map, or archive extraction differ; inspect artifacts")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    # Never truncate an existing experiment, even if it contains a failed run.
    args.out.mkdir(parents=True, exist_ok=False)
    out = args.out.resolve()
    (out / "logs").mkdir()
    recorder = Recorder(out)
    summary = {"scope": "EXP-00 link-only; not MCU firmware", "status": "running",
               "measurements": {"stack_bytes": None, "heap_peak_bytes": None,
                                "cycles": None, "latency": None}}
    write_json(out / "summary.json", summary)
    try:
        manifest = {"schema": 1, "python": sys.version, "host": platform.platform(),
                    "repository": snapshot(recorder), "locale": "C",
                    "environment": {key: os.environ.get(key) for key in (
                        "PATH", "GCC_EXEC_PREFIX", "COMPILER_PATH", "LIBRARY_PATH", "CPATH",
                        "CPLUS_INCLUDE_PATH", "C_INCLUDE_PATH", "LD_LIBRARY_PATH", "SOURCE_DATE_EPOCH")}}
        write_json(out / "manifest.json", manifest)
        compiler = args.compiler.resolve()
        manifest["toolchain"] = toolchain(recorder, compiler)
        write_json(out / "manifest.json", manifest)
        audits = []
        for name in ("run1", "run2"):
            recorder.run([sys.executable, PROBE, "--compiler", compiler, "--out", out / name])
            audits.append(audit(recorder, out / name, compiler))
            write_json(out / f"{name}.audit.json", audits[-1])
        compare(*audits)
        summary.update(status="passed", build_results=36, rows=audits[0]["rows"],
                       agreement="metrics, symbols, ELF, binary, normalized map, archive extraction")
    except Exception as error:
        summary.update(status="failed", error=f"{type(error).__name__}: {error}")
        print(summary["error"], file=sys.stderr)
    finally:
        write_json(out / "summary.json", summary)
        hashes = {p.relative_to(out).as_posix(): digest(p) for p in sorted(out.rglob("*"))
                  if p.is_file() and p.name != "sha256.json"}
        write_json(out / "sha256.json", hashes)
    if summary["status"] != "passed":
        return 1
    print(f"EXP-00 passed: 36 build results; evidence in {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
