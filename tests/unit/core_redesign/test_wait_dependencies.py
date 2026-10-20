"""Resource-held waits: live serial ownership versus declared pump capability."""
from itertools import product
from pathlib import Path
import random
import shutil
import subprocess

import pytest

FIELDS = ("candidate mode where profile root_kind root_opt helper_kind helper_opt "
          "owner0 owner1 owner2 need1 need2 need3 deps1 deps2 deps3 known timeout due release inject context preloaded now").split()


def scenario(**changes):
    s = dict(candidate=0, mode=1, where=0, profile=0, root_kind=1, root_opt=0, helper_kind=1, helper_opt=0,
             owner0=-1, owner1=-1, owner2=-1, need1=1, need2=0, need3=0, deps1=0, deps2=0, deps3=0,
             known=15, timeout=9, due=1, release=0, inject=0, context=0, preloaded=0, now=0xFFFFFFFC)
    s.update(changes)
    return s


def supports(kind, opted, s):
    if kind in [0, 3]: return True
    return s["where"] == 0 or (s["profile"] != 0 and (kind == 1 or (s["profile"] == 2 and opted)))


def inspect(owners, needs, deps, s):
    # Independent graph traversal, with path-local recursion for cycle testing.
    roles = [(1, False), (s["root_kind"], s["root_opt"]), (s["helper_kind"], s["helper_opt"]), (1, False)]
    edges = [set(i for i in range(4) if deps[a] & (1 << i)) for a in range(4)]
    unknown, invalid = set(), set()
    for a in range(4):
        if not s["known"] & (1 << a): unknown.add(a)
        if needs[a] & ~7 or deps[a] & ~15: invalid.add(a)
        for r in range(3):
            if not needs[a] & (1 << r): continue
            owner = owners[r]
            if owner == -2: unknown.add(a)
            elif owner != -1:
                if owner not in range(4): invalid.add(a)
                else: edges[a].add(owner)
    reached, todo = set(), [1]
    while todo:
        actor = todo.pop()
        if actor in reached: continue
        reached.add(actor); todo.extend(edges[actor])
    if reached & invalid: return 6
    if 0 in reached: return 2
    if reached & unknown: return 3
    if any(not supports(*roles[a], s) for a in reached): return 4
    def cycle(actor, path):
        if actor in path: return True
        return any(cycle(other, path | {actor}) for other in edges[actor])
    if cycle(1, set()): return 5
    return 1


def model(s):
    owners = [s[f"owner{r}"] for r in range(3)]
    needs = [0, *[s[f"need{i}"] for i in range(1, 4)]]
    deps = [0, *[s[f"deps{i}"] for i in range(1, 4)]]
    roles = [(1, False), (s["root_kind"], s["root_opt"]), (s["helper_kind"], s["helper_opt"]), (1, False)]
    elapsed = drives = calls = produced = done = checks = reason = 0
    available = bool(s["preloaded"]); value = 99
    def row(status):
        return [status, elapsed, drives, calls, produced, int(available), done, *owners, reason, checks, value]
    if s["mode"] <= 1 and s["context"] != 0: return row(3)
    if s["timeout"] >= 2**31: return row(4)
    if s["mode"] >= 2 and s["context"] in [1, 3]: return row(3)
    if available: available = False; value = 17; return row(0)
    if s["timeout"] == 0: return row(2)
    if s["mode"] <= 1 and not supports(*roles[1], s): return row(5)
    for _ in range(32):
        if s["mode"] == 1:
            checks += 1; reason = inspect(owners, needs, deps, s)
            if reason != 1: return row(7)
        drives += 1; elapsed += 1
        if s["mode"] >= 2 and s["release"] and elapsed == s["release"]:
            owners = [-1 if owner == 0 else owner for owner in owners]
        if s["inject"] and elapsed == s["inject"]: owners[0] = 0
        order = [a for a in [2, 3, 1] if roles[a][0] in [0, 3]]
        order += [a for a in [2, 3, 1] if roles[a][0] in [1, 2]]
        for a in order:
            if done & (1 << a) or not supports(*roles[a], s): continue
            if roles[a][0] in [1, 2]: calls += 1
            if deps[a] & ~done or (a == 1 and elapsed < s["due"]): continue
            if any(needs[a] & (1 << r) and owners[r] != -1 for r in range(3)): continue
            for r in range(3):
                if needs[a] & (1 << r): owners[r] = a
            if a == 1: produced += 1; available = True
            done |= 1 << a
            deps = [mask & ~(1 << a) for mask in deps]
            owners = [-1 if owner == a else owner for owner in owners]
        if available: available = False; value = 17; return row(0)
        if elapsed >= s["timeout"]: return row(2)
    assert s["mode"] >= 2
    return row(1)


@pytest.fixture(scope="module")
def native_dependencies(tmp_path_factory):
    executable = tmp_path_factory.mktemp("exp03-dependencies") / "control"
    compiler = shutil.which("g++"); assert compiler
    result = subprocess.run([
        compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
        str(Path(__file__).with_name("dependency_driver.cpp")), "-o", str(executable),
    ], capture_output=True, text=True, timeout=120)
    assert result.returncode == 0, result.stderr
    return executable


def run(executable, cases):
    result = subprocess.run([str(executable)], input="\n".join(" ".join(str(s[f]) for f in FIELDS) for s in cases) + "\n",
                            capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    assert not result.stderr
    rows = [list(map(int, row.split())) for row in result.stdout.splitlines()]
    assert len(rows) == len(cases)
    for s, row in zip(cases, rows): assert row == model(s), (s, row, model(s))
    return rows


@pytest.mark.parametrize("candidate", [0, 1, 2], ids=["A", "B", "C"])
def test_literal_owned_resources_and_guard(native_dependencies, candidate):
    inputs = [
        dict(owner0=0),  # waiter holds bus
        dict(owner1=0, need1=2),  # waiter holds buffer
        dict(owner2=0, need1=4),  # waiter holds lock
        dict(owner0=2, owner1=0, need2=2),  # indirect owner -> waiter
        dict(owner0=2, owner1=3, need2=2, need3=1),  # reachable cycle
        dict(owner0=2, owner1=3, need2=2),  # runnable holder chain
        dict(owner0=2, helper_kind=2, where=2, profile=2),  # unopted user holder
        dict(owner0=2, helper_kind=2, helper_opt=1, where=2, profile=2),
        dict(owner0=2, helper_kind=2, helper_opt=1, where=2, profile=1),
        dict(known=13), dict(owner0=-2), dict(owner0=1),
        dict(due=5, inject=1),  # live dependency appears after first pump
        dict(owner0=0, release=3),  # only explicit asynchronous caller can release
        dict(owner0=0, preloaded=1), dict(owner0=0, timeout=0),
        dict(owner0=0, context=2), dict(owner0=0, timeout=0x80000000),
        dict(deps1=1 << 2, deps2=1 << 3), dict(deps1=1),
    ]
    cases = [scenario(candidate=candidate, mode=mode, **x) for x in inputs for mode in [0, 1, 2]]
    rows = run(native_dependencies, cases)
    assert rows[0][0] == 2 and rows[0][4] == 0 and rows[0][7] == 0
    assert rows[1][0] == 7 and rows[1][2] == 0 and rows[1][10] == 2 and rows[1][7] == 0
    assert rows[2][0] == 2 and rows[2][7] == 0
    # input 13: release at t=3 belongs to nonblocking user code, not wait_for.
    assert rows[39][0] == 2 and rows[40][0] == 7
    assert rows[41][0] == 0 and rows[41][1] == 3 and rows[41][7] == -1
    # A changed snapshot is checked again, before the second guarded pump.
    assert rows[37][0] == 7 and rows[37][1] == 1 and rows[37][11] == 2


@pytest.mark.parametrize("candidate", [0, 1, 2], ids=["A", "B", "C"])
def test_scope_and_dependency_matrix(native_dependencies, candidate):
    cases = [scenario(candidate=candidate, mode=mode, where=where, profile=profile,
                      root_kind=root, root_opt=ropt, helper_kind=helper, helper_opt=hopt, owner0=2)
             for mode, where, profile, root, ropt, helper, hopt in
             product([0, 1, 2], range(3), range(3), range(4), [0, 1], [1, 2], [0, 1])]
    run(native_dependencies, cases)


@pytest.mark.parametrize("seed", range(10))
def test_generated_ownership_and_wait_inputs(native_dependencies, seed):
    rng = random.Random(seed); cases = []
    for _ in range(300):
        cases.append(scenario(mode=rng.randrange(3), where=rng.randrange(3), profile=rng.randrange(3),
            root_kind=rng.randrange(4), root_opt=rng.randrange(2), helper_kind=rng.choice([1, 2]), helper_opt=rng.randrange(2),
            owner0=rng.randrange(-2, 4), owner1=rng.randrange(-2, 4), owner2=rng.randrange(-2, 4),
            need1=rng.randrange(8), need2=rng.randrange(8), need3=rng.randrange(8),
            deps1=rng.randrange(16), deps2=rng.randrange(16), deps3=rng.randrange(16),
            known=rng.randrange(16), timeout=rng.choice([0, 1, 9, 40, 0x80000000]), due=rng.randrange(6),
            release=rng.randrange(6), inject=rng.randrange(4), context=rng.randrange(4),
            preloaded=rng.randrange(2), now=rng.choice([0, 0xFFFFFFFC])))
    # Synchronous clock always progresses: keep long timeouts finite too.
    for s in cases:
        if s["mode"] <= 1 and s["timeout"] == 40: s["timeout"] = 20
    for candidate in [0, 1, 2]: run(native_dependencies, [dict(s, candidate=candidate) for s in cases])


def test_bounded_graph_cycles_diamonds_and_invalid_metadata(native_dependencies):
    result = subprocess.run([str(native_dependencies), "contracts"], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    assert not result.stdout and not result.stderr
