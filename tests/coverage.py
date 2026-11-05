#!/usr/bin/env python3
"""The hardware tests' coverage: what tests/coverage.toml asks for against the results of run_hardware.py runs.

  uv run --no-project python tests/coverage.py <run directory or result.json> ... [--markdown out.md]

A run directory is run_hardware.py's --out (result.json, places.json, cases/<target>/<case>/junit.xml). Each test's
outcome comes from its junit file; the target's series from its chip (places.json). For every item:

- scope "series": one table row per series, the best outcome on that series (PASS beats FAIL beats SKIP), n/a
  where the series lacks the item's peripheral, blank where nothing ran.
- scope "once": the best outcome anywhere, and where it was.

Exit status 0; the report is the output (stdout, or --markdown)."""
import argparse
import json
import pathlib
import sys
import tomllib
import xml.etree.ElementTree as ET

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(HERE))
from harness.series import series_capabilities  # noqa: E402

RANK = {"PASS": 3, "FAIL": 2, "SKIP": 1}
MARK = {"PASS": "✓", "FAIL": "✗", "SKIP": "skip", None: "", "n/a": "n/a"}


def load_items():
    return tomllib.loads((HERE / "coverage.toml").read_text())["item"]


def series_of(chip, names):
    """The longest series name the chip starts with (CH32V003F4U6 -> CH32V003)."""
    found = [s for s in names if chip.upper().startswith(s.upper())]
    return max(found, key=len) if found else None


def junit_outcomes(path):
    """{test name: PASS / FAIL / SKIP} of a junit file."""
    out = {}
    try:
        root = ET.parse(path).getroot()
    except (OSError, ET.ParseError):
        return out
    for case in root.iter("testcase"):
        name = case.get("name", "")
        if case.find("failure") is not None or case.find("error") is not None:
            out[name] = "FAIL"
        elif case.find("skipped") is not None:
            out[name] = "SKIP"
        else:
            out[name] = "PASS"
    return out


def collect(paths, names):
    """[(series, target, case, test name, outcome, run)] over the runs."""
    rows = []
    for p in paths:
        p = pathlib.Path(p)
        run = p.parent if p.name == "result.json" else p
        result = run / "result.json"
        if not result.exists():
            print(f"skip {run}: no result.json", file=sys.stderr)
            continue
        report = json.loads(result.read_text())
        # places.json (a target named by its address); bench.json in runs before 2026-11-05
        if (run / "places.json").exists():
            targets = json.loads((run / "places.json").read_text())
        else:
            bench = json.loads((run / "bench.json").read_text()) if (run / "bench.json").exists() else {}
            targets = bench.get("targets", bench)
        for entry in report.get("cases", []):
            target, case = entry["target"], entry["case"]
            chip = (targets.get(target) or {}).get("chip", "")
            series = series_of(chip, names)
            tests = junit_outcomes(run / "cases" / target / case / "junit.xml")
            if not tests:   # compile-only, not run, or the case failed before pytest: the case's own status
                status = {"PASS": "PASS", "FAIL": "FAIL", "SKIP": "SKIP"}.get(entry.get("status"))
                if status:
                    rows.append((series, target, case, "", status, run.name))
                continue
            for name, outcome in tests.items():
                rows.append((series, target, case, name, outcome, run.name))
    return rows


def matches(item, case, name):
    want_case, _, want_test = item["test"].partition("::")
    return case == want_case and (not want_test or name == want_test or name.startswith(want_test + "["))


def applies(item, caps, series=None):
    if series in item.get("also", []):
        return True
    need = set(item.get("needs", []))
    any_of = set(item.get("any_of", []))
    return need <= caps and (not any_of or bool(any_of & caps))


def best(outcomes):
    return max(outcomes, key=lambda o: RANK[o]) if outcomes else None


def report(rows, items, caps):
    series_list = sorted(s for s in caps if s.startswith("CH32"))
    lines = ["# Hardware test coverage", ""]
    per_series = [i for i in items if i["scope"] == "series"]
    once = [i for i in items if i["scope"] == "once"]
    # series x items
    lines.append("## Every series (scope = series)")
    lines.append("")
    lines.append("| series | " + " | ".join(i["id"] for i in per_series) + " |")
    lines.append("|---|" + "---|" * len(per_series))
    done = {i["id"]: 0 for i in per_series}
    wanted = {i["id"]: 0 for i in per_series}
    for s in series_list:
        cells = []
        for item in per_series:
            if not applies(item, caps[s], s):
                cells.append(MARK["n/a"])
                continue
            wanted[item["id"]] += 1
            outcome = best([o for (ser, _, case, name, o, _) in rows if ser == s and matches(item, case, name)])
            done[item["id"]] += outcome == "PASS"
            cells.append(MARK[outcome])
        lines.append(f"| {s} | " + " | ".join(cells) + " |")
    lines.append("| **passed / series** | " + " | ".join(f"{done[i['id']]} / {wanted[i['id']]}" for i in per_series) + " |")
    lines.append("")
    lines.append("## Anywhere (scope = once)")
    lines.append("")
    lines.append("| item | result | where |")
    lines.append("|---|---|---|")
    for item in once:
        hits = [(o, ser, target, run) for (ser, target, case, name, o, run) in rows if matches(item, case, name)]
        outcome = best([h[0] for h in hits])
        where = sorted({f"{h[1]} ({h[2]}, {h[3]})" for h in hits if h[0] == outcome}) if outcome else []
        lines.append(f"| {item['id']} | {MARK[outcome] or 'not run'} | {', '.join(where[:3])} |")
    lines.append("")
    total = sum(wanted.values())
    lines.append(f"Series items: {sum(done.values())} of {total} passed; once items: "
                 f"{sum(1 for i in once if best([o for (_, _, c, n, o, _) in rows if matches(i, c, n)]) == 'PASS')} "
                 f"of {len(once)} passed.")
    return "\n".join(lines) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("runs", nargs="*", help="run_hardware.py --out directories, or their result.json")
    ap.add_argument("--markdown", type=pathlib.Path, help="write the report here instead of stdout")
    args = ap.parse_args(argv)
    caps = {s: set(c) for s, c in series_capabilities().items()}
    text = report(collect(args.runs, list(caps)), load_items(), caps)
    if args.markdown:
        args.markdown.write_text(text, encoding="utf-8")
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
