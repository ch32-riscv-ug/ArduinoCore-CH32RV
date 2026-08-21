#!/usr/bin/env python3
"""W-7: size regression gate for the compile matrix.

The toolchain is pinned, so builds are byte-deterministic: the committed
baseline must match EXACTLY. When a size changes on purpose, regenerate the
baseline in the same PR (--update) so the change is part of the review.

Usage:
  check_sizes.py --baseline sizes_baseline.json --sizes <work>/sizes.tsv --check
  check_sizes.py --baseline sizes_baseline.json --sizes <work>/sizes.tsv --update
"""
import argparse
import json
import pathlib
import sys


def read_tsv(path: pathlib.Path) -> dict:
    out = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        pnum, text, data, bss = line.split("\t")
        out[pnum] = {"text": int(text), "data": int(data), "bss": int(bss)}
    return dict(sorted(out.items()))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--baseline", required=True, type=pathlib.Path)
    ap.add_argument("--sizes", required=True, type=pathlib.Path)
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--update", action="store_true")
    args = ap.parse_args()

    current = read_tsv(args.sizes)
    if args.update:
        # sort_keys, because otherwise a rebuild rewrites every entry in
        # TSV column order and one changed number hides in a 244-line diff.
        args.baseline.write_text(
            json.dumps(current, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
        print(f"wrote baseline: {args.baseline} ({len(current)} entries)")
        return 0

    baseline = json.loads(args.baseline.read_text(encoding="utf-8"))
    fail = 0
    for pnum in sorted(set(baseline) | set(current)):
        b, c = baseline.get(pnum), current.get(pnum)
        if b is None:
            print(f"NEW:   {pnum} {c} (not in baseline)"); fail = 1
        elif c is None:
            print(f"GONE:  {pnum} (in baseline, not built)"); fail = 1
        elif b != c:
            diff = {k: c[k] - b[k] for k in c if c[k] != b[k]}
            print(f"DIFF:  {pnum} {b} -> {c} (delta {diff})"); fail = 1
    if fail:
        print("size baseline mismatch. If intentional, regenerate with --update "
              "and commit the new baseline in the same PR.")
    else:
        print(f"sizes match baseline ({len(current)} entries)")
    return fail


if __name__ == "__main__":
    sys.exit(main())
