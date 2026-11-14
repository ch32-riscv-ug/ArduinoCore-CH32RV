#!/usr/bin/env python3
"""The hardware tests' coverage: what tests/coverage.toml asks for against the results of run_hardware.py runs.

  uv run --no-project python tests/coverage.py <run directory or result.json> ... [--markdown out.md]

A run directory is run_hardware.py's --out (result.json, places.json, cases/<target>/<case>/junit.xml). Each test's
outcome comes from its junit file; the target's series from its chip (places.json). For every item:

- scope "series": one table row per series, the outcome of the latest run that ran the item on that series (within a
  run, PASS beats FAIL beats SKIP), with the best of the earlier runs after it when that was better ("✗ (✓ before)":
  a regression shows); n/a where the series lacks the item's peripheral, blank where nothing ran. Items with
  kind = "self" (the DUT reads its own pads back) get a table of their own: they do not stand for an outside
  observation (TEST_PLAN.ja.md).
- scope "once": the same over every series, and where the shown outcome came from.
- a test that skips with a reason starting "n/a:" says the chip has no such thing (the DUT answered so): n/a, not
  counted as wanted. A case that failed before pytest (build, upload) counts as a failure of each of its items.
- `missing`: series (or parts) whose silicon has the item but the core does not implement it yet: "未実装" there,
  counted in the wanted numbers - a feature not built is a gap, never n/a (only BLE is left out, the user).
- an item with no `test` is a feature the tests do not cover yet: "no test" in each cell it applies to, counted in the
  wanted numbers, so the gap shows.

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

RANK = {"PASS": 4, "FAIL": 3, "SKIP": 2, "NA": 1}
MARK = {"PASS": "✓", "FAIL": "✗", "SKIP": "skip", "NA": "n/a", None: "", "n/a": "n/a", "no test": "no test",
        "missing": "未実装"}


def load_items():
    return tomllib.loads((HERE / "coverage.toml").read_text())["item"]


def series_of(chip, names):
    """The longest series name the chip starts with (CH32V003F4U6 -> CH32V003)."""
    found = [s for s in names if chip.upper().startswith(s.upper())]
    return max(found, key=len) if found else None


def junit_outcomes(path):
    """{test name: PASS / FAIL / SKIP / NA} of a junit file (NA: skipped with a reason starting "n/a:")."""
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
            message = case.find("skipped").get("message", "")
            out[name] = "NA" if message.removeprefix("Skipped: ").startswith("n/a:") else "SKIP"
        else:
            out[name] = "PASS"
    return out


def collect(paths, names):
    """[(series, target, case, test name, outcome, run)] over the runs, the oldest run first (result.json's time)."""
    rows = []
    def when(p):
        p = pathlib.Path(p)
        result = p if p.name == "result.json" else p / "result.json"
        return result.stat().st_mtime if result.exists() else 0
    for p in sorted(paths, key=when):
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


def tests_of(item):
    """The item's tests: `test` is one "<case>[::<test>]" or a list of them (any of them counts); none: not covered."""
    test = item.get("test", [])
    return [test] if isinstance(test, str) else list(test)


def matches(item, case, name):
    for test in tests_of(item):
        want_case, _, want_test = test.partition("::")
        # name "": the case failed before pytest - every item of the case shares that
        if case == want_case and (not want_test or not name or name == want_test or name.startswith(want_test + "[")):
            return True
    return False


def applies(item, caps, series=None):
    if series in item.get("not_on", []):
        return False
    if series in item.get("also", []):
        return True
    need = set(item.get("needs", []))
    any_of = set(item.get("any_of", []))
    return need <= caps and (not any_of or bool(any_of & caps))


def best(outcomes):
    return max(outcomes, key=lambda o: RANK[o]) if outcomes else None


def latest(hits):
    """(the latest run's outcome, the best earlier outcome when better, the rows giving the shown outcome) of
    [(outcome, run, row)], the runs in collect()'s order."""
    if not hits:
        return None, None, []
    runs = list(dict.fromkeys(run for _, run, _ in hits))
    last = runs[-1]
    now = best([o for o, run, _ in hits if run == last])
    before = best([o for o, run, _ in hits if run != last])
    better = before if before and RANK[before] > RANK[now] else None
    return now, better, [row for o, run, row in hits if run == last and o == now]


def cell(now, better):
    return MARK[now] + (f" ({MARK[better]} before)" if better else "")


def report(rows, items, caps):
    series_list = sorted(s for s in caps if s.startswith("CH32"))
    lines = ["# Hardware test coverage", ""]
    once = [i for i in items if i["scope"] == "once"]
    passed = wanted_total = 0
    for title, kind in (("Every series (scope = series)", "external"),
                        ("Every series, self-checks (scope = series, kind = self: the DUT reads itself back)", "self")):
        per_series = [i for i in items if i["scope"] == "series" and i.get("kind", "external") == kind]
        lines.append(f"## {title}")
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
                if s in item.get("missing", []):
                    wanted[item["id"]] += 1
                    cells.append(MARK["missing"])
                    continue
                if not tests_of(item):
                    wanted[item["id"]] += 1
                    cells.append(MARK["no test"])
                    continue
                now, better, _ = latest([(o, run, None) for (ser, _, case, name, o, run) in rows
                                         if ser == s and matches(item, case, name)])
                wanted[item["id"]] += now != "NA"
                done[item["id"]] += now == "PASS"
                cells.append(cell(now, better))
            lines.append(f"| {s} | " + " | ".join(cells) + " |")
        lines.append("| **passed / series** | " + " | ".join(f"{done[i['id']]} / {wanted[i['id']]}" for i in per_series) + " |")
        lines.append("")
        passed += sum(done.values())
        wanted_total += sum(wanted.values())
    lines.append("## Anywhere (scope = once)")
    lines.append("")
    lines.append("| item | result | where |")
    lines.append("|---|---|---|")
    once_passed = 0
    for item in once:
        if not tests_of(item):
            lines.append(f"| {item['id']} | no test |  |")
            continue
        now, better, shown = latest([(o, run, (ser, target, run)) for (ser, target, case, name, o, run) in rows
                                     if matches(item, case, name)])
        once_passed += now == "PASS"
        where = sorted({f"{ser} ({target}, {run})" for ser, target, run in shown})
        lines.append(f"| {item['id']} | {cell(now, better) if now else 'not run'} | {', '.join(where[:3])} |")
    lines.append("")
    lines.append(f"Series items: {passed} of {wanted_total} passed in their latest runs; once items: "
                 f"{once_passed} of {len(once)} passed.")
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
