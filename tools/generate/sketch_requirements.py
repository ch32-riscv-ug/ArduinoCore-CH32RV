"""Example requirements from generated board limits and silicon capabilities."""
import functools
import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parents[2]

SIZE_SUFFIX = {"K": 1024, "M": 1024 * 1024}


class BadRequirement(Exception):
    """A `requires:` line the parser cannot honour. Never guess past one."""


@functools.lru_cache(maxsize=1)
def all_boards() -> tuple:
    """Every series in the generated boards.txt, in file order.

    One board per series and no pnum sweep: pnum picks the linker script and
    the flash/RAM limits, not the variant, so building one sketch for twelve
    CH32V203 part numbers compiles twelve identical translation units. A series
    is where the pads, the peripherals, the vector table and the ISA change,
    which is the axis worth paying for.
    """
    text = (REPO / "boards.txt").read_text(encoding="utf-8")
    return tuple(dict.fromkeys(re.findall(r"^([A-Za-z0-9_]+)\.name=", text, re.M)))


@functools.lru_cache(maxsize=1)
def series_capabilities() -> dict:
    """series -> the peripherals its variant header names a clock enable for."""
    caps = {}
    for header in sorted((REPO / "variants").glob("*/pins_arduino.h")):
        caps[header.parent.name] = frozenset(
            re.findall(r"CH32RV_CLKEN_([A-Z0-9_]+)_ADDR",
                       header.read_text(encoding="utf-8")))
    return caps


@functools.lru_cache(maxsize=1)
def series_limits() -> dict:
    """series -> (flash, RAM) of its ANY menu entry."""
    text = (REPO / "boards.txt").read_text(encoding="utf-8")
    out: dict = {}
    for key, index in (("upload.maximum_size", 0),
                       ("upload.maximum_data_size", 1)):
        for board, value in re.findall(
                rf"^([A-Za-z0-9_]+)\.menu\.pnum\.ANY\.{key}=(\d+)$", text, re.M):
            pair = list(out.get(board, (0, 0)))
            pair[index] = int(value)
            out[board] = tuple(pair)
    return out


def requirements(src: pathlib.Path) -> dict:
    """{"caps": [...], "flash": int, "ram": int} from the sketch's own .ino."""
    ino = src / f"{src.name}.ino"
    if not ino.exists():
        return {"caps": [], "flash": 0, "ram": 0}
    match = re.search(r"requires:\s*([^*\n]+)", ino.read_text(encoding="utf-8"))
    need = {"caps": [], "flash": 0, "ram": 0}
    if not match:
        return need
    known = set().union(*series_capabilities().values())
    for item in match.group(1).replace(",", " ").split():
        key, sep, value = item.partition("=")
        if not sep:
            cap = key.upper()
            if cap not in known:
                # A typo would otherwise read as "no series has this", which
                # skips every board and passes with nothing built.
                raise BadRequirement(
                    f"{ino.name}: no series declares a capability {cap!r}. "
                    f"Capabilities are the <X> of CH32RV_CLKEN_<X>_ADDR in "
                    f"variants/*/pins_arduino.h.")
            need["caps"].append(cap)
            continue
        if key not in ("flash", "ram"):
            raise BadRequirement(f"{ino.name}: unknown requires: key {key!r}")
        scale = SIZE_SUFFIX.get(value[-1].upper(), 1)
        need[key] = int(value[:-1] if scale > 1 else value) * scale
    return need


def unmet(need: dict, board: str):
    """Why `board` cannot run this sketch, in the sketch's own words, or None."""
    caps = series_capabilities().get(board, ())
    missing = [c for c in need["caps"] if c not in caps]
    if missing:
        return f"requires {', '.join(missing)}, which {board} does not have"
    flash, ram = series_limits().get(board, (0, 0))
    if need["flash"] and flash < need["flash"]:
        return (f"requires {need['flash'] // 1024} KB flash, "
                f"{board} ANY has {flash // 1024} KB")
    if need["ram"] and ram < need["ram"]:
        return (f"requires {need['ram'] // 1024} KB RAM, "
                f"{board} ANY has {ram // 1024} KB")
    return None
