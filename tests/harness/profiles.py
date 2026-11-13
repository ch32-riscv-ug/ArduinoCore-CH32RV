"""Write the bench's profiles (tests/hw/profiles.toml) into every tests/hw/<test>/sketch.yaml.

  uv run --project tests --locked python -m harness.profiles          # from tests/: write
  uv run --project tests --locked python -m harness.profiles --check  # fail when a sketch.yaml is not current

A profile is a board on the bench (or a configuration of one): its fqbn comes from boards.txt for the part, with the
profile's menu options; its console is SerialDMSeq through ch32rv's monitor (port_config) unless the test's
directory names another source (CONSOLE). No platform version is written: the hardware tests build the work
tree's core, which harness/pytest_bench.py links into an isolated Arduino user directory."""
import argparse
import pathlib
import sys
import tomllib

TESTS = pathlib.Path(__file__).resolve().parents[1]
HW = TESTS / "hw"
HEADER = "# Written by harness/profiles.py from tests/hw/profiles.toml - edit those, not this file.\n"
# A test directory whose sketch talks through another console than SerialDMSeq.
CONSOLE = {"console_rtt": "rtt", "console_dmdata": "dmdata", "console_sdi": "sdi"}


def load(path=HW / "profiles.toml"):
    return tomllib.loads(path.read_text(encoding="utf-8"))


def sketch_yaml(directory, profiles):
    sys.path.insert(0, str(TESTS))
    from harness import place
    source = CONSOLE.get(directory.name, "dmseq")
    lines = [HEADER, "profiles:\n"]
    for name, spec in profiles.items():
        fqbn, _ = place.with_options(*place.fqbn_of(spec["part"]), spec.get("options", ""))
        lines += [f"  {name}:\n", f"    fqbn: {fqbn}\n", "    port_config:\n",
                  f"      source: {source}\n", f"      chip: {spec['part']}\n"]
    return "".join(lines)


def sketches(root=HW):
    return sorted(d for d in root.iterdir() if d.is_dir() and (d / f"{d.name}.ino").exists())


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true", help="only report sketch.yaml files that are not current")
    args = ap.parse_args(argv)
    profiles = load()
    stale = []
    for directory in sketches():
        path = directory / "sketch.yaml"
        text = sketch_yaml(directory, profiles)
        if path.exists() and path.read_text(encoding="utf-8") == text:
            continue
        stale.append(path)
        if not args.check:
            path.write_text(text, encoding="utf-8")
    for path in stale:
        print(("stale: " if args.check else "wrote: ") + str(path.relative_to(TESTS)))
    return 1 if args.check and stale else 0


if __name__ == "__main__":
    raise SystemExit(main())
