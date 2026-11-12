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


# Capabilities that are one of several peripherals: a series has the name when
# it has any of them. USB_DEVICE is a controller CH32RVUsbDevice drives (V103
# and X035's USBFS, V20x's USB (USBD), V30x / H41x's USBHS, V407's USBHS1 / 2);
# the V00x / M007 series have none, so a USB device example is not built there.
DERIVED = {"USB_DEVICE": frozenset({"USB", "USBFS", "USBHS", "USBHS1", "USBHS2"})}


@functools.lru_cache(maxsize=1)
def ethernet_series() -> frozenset:
    """The series the Network library has an ETH for: the CH32RV_SERIES_<X>
    its NetworkEthernet.h names where it defines NETWORK_ETHERNET. A part-only
    condition (CH32RV_PART_CH32V203RBT6) does not count here: the series' ANY
    part has no ETH (ethernet_parts())."""
    return frozenset(s for cond in _ethernet_conditions()
                     for s in re.findall(r"CH32RV_SERIES_(\w+)", cond))


def _ethernet_conditions() -> list:
    text = (REPO / "libraries/Network/src/NetworkEthernet.h").read_text(encoding="utf-8")
    return re.findall(r"^#(?:el)?if\b(.*(?:\\\n.*)*)\n#define NETWORK_ETHERNET 1", text, re.M)


@functools.lru_cache(maxsize=1)
def ethernet_parts() -> dict:
    """part -> series, for the parts NetworkEthernet.h gives an ETH by part
    number in a series that has none (CH32V203RBT6): the ANY entry, sized for
    every part (V203: 32K / 10K), could not hold the stack anyway, so such an
    example gets a profile for the part itself. A `!defined(CH32RV_PART_...)`
    takes a part away and is not one."""
    text = (REPO / "boards.txt").read_text(encoding="utf-8")
    out = {}
    for cond in _ethernet_conditions():
        for part in re.findall(r"(?<!!)defined\(CH32RV_PART_(\w+)\)", cond):
            board = re.search(rf"^([A-Za-z0-9_]+)\.menu\.pnum\.{part}\.build\.part=", text, re.M)
            if board and board.group(1) not in ethernet_series():
                out[part] = board.group(1)
    return out


@functools.lru_cache(maxsize=1)
def series_capabilities() -> dict:
    """series -> the peripherals its variant header names a clock enable for,
    plus the DERIVED names it has one of, SERIAL1 where it has a second USART
    or UART (Serial1, CH32RV_SERIAL_COUNT >= 2), and ETHERNET where the
    Network library has an ETH (ethernet_series())."""
    caps = {}
    for header in sorted((REPO / "variants").glob("*/pins_arduino.h")):
        found = frozenset(re.findall(r"CH32RV_CLKEN_([A-Z0-9_]+)_ADDR",
                                     header.read_text(encoding="utf-8")))
        extra = {name for name, any_of in DERIVED.items() if found & any_of}
        if len([c for c in found if re.fullmatch(r"U(S)?ART\d+", c)]) >= 2:
            extra.add("SERIAL1")
        if header.parent.name in ethernet_series():
            extra.add("ETHERNET")
        caps[header.parent.name] = found | extra
    return caps


@functools.lru_cache(maxsize=1)
def series_defines() -> dict:
    """series -> the names its variant header #defines (pads such as PF4,
    analog pins such as A0, default routes such as CH32RV_SPI1_SCK)."""
    return {header.parent.name: frozenset(re.findall(r"^#define\s+(\w+)",
                                                     header.read_text(encoding="utf-8"), re.M))
            for header in sorted((REPO / "variants").glob("*/pins_arduino.h"))}


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
        # A series with a flash/RAM split menu sets the sizes there; its
        # first entry is the default (the factory split).
        for board, value in re.findall(
                rf"^([A-Za-z0-9_]+)\.menu\.ramsplit\.[A-Za-z0-9_]+\.{key}=(\d+)$",
                text, re.M):
            pair = list(out.get(board, (0, 0)))
            if pair[index] == 0:
                pair[index] = int(value)
                out[board] = tuple(pair)
    return out


def requirements(src: pathlib.Path) -> dict:
    """{"caps": [...], "flash": int, "ram": int} from the sketch's own .ino."""
    ino = src / f"{src.name}.ino"
    if not ino.exists():
        return {"caps": [], "defines": [], "flash": 0, "ram": 0}
    match = re.search(r"requires:\s*([^*\n]+)", ino.read_text(encoding="utf-8"))
    need = {"caps": [], "defines": [], "flash": 0, "ram": 0}
    if not match:
        return need
    known = set().union(*series_capabilities().values())
    defined = set().union(*series_defines().values())
    for item in match.group(1).replace(",", " ").split():
        key, sep, value = item.partition("=")
        if not sep:
            # A capability (upper case, case-insensitive as before), or a name
            # the variant must #define: a pad, an analog pin, a default route.
            cap = key.upper()
            if cap in known:
                need["caps"].append(cap)
                continue
            if key in defined:
                need["defines"].append(key)
                continue
            # A typo would otherwise read as "no series has this", which
            # skips every board and passes with nothing built.
            raise BadRequirement(
                f"{ino.name}: no series declares a capability {cap!r} and no "
                f"variant defines {key!r}. Capabilities are the <X> of "
                f"CH32RV_CLKEN_<X>_ADDR in variants/*/pins_arduino.h, or "
                f"{', '.join([*DERIVED, 'SERIAL1', 'ETHERNET'])}.")
        if key not in ("flash", "ram"):
            raise BadRequirement(f"{ino.name}: unknown requires: key {key!r}")
        scale = SIZE_SUFFIX.get(value[-1].upper(), 1)
        need[key] = int(value[:-1] if scale > 1 else value) * scale
    return need


def target_board(src: pathlib.Path):
    """The one board a board-specific example is for, or None.

    `/* board: CH32X035:pnum=CH32X035F8U6 */` names a boards.txt entry and its
    menu options (the fqbn after `ch32-riscv-ug:ch32rv:`). Such an example - a
    product board's template - is built for that board only, instead of one
    generic board per series.
    """
    ino = src / f"{src.name}.ino"
    if not ino.exists():
        return None
    match = re.search(r"^\s*(?:/\*|\*)\s*board:\s*([A-Za-z0-9_]+)((?::[A-Za-z0-9_]+=[A-Za-z0-9_]+(?:,[A-Za-z0-9_]+=[A-Za-z0-9_]+)*)?)\s",
                      ino.read_text(encoding="utf-8"), re.M)
    if not match:
        return None
    board, options = match.group(1), match.group(2).lstrip(":")
    if board not in all_boards():
        raise BadRequirement(f"{ino.name}: no board {board!r} in boards.txt")
    text = (REPO / "boards.txt").read_text(encoding="utf-8")
    for option in filter(None, options.split(",")):
        menu, _, value = option.partition("=")
        if not re.search(rf"^{board}\.menu\.{menu}\.{value}=", text, re.M):
            raise BadRequirement(f"{ino.name}: {board} has no {menu}={value}")
    return board + (":" + options if options else "")


@functools.lru_cache(maxsize=1)
def ethernet_clocks() -> dict:
    """series -> its CPU Clock menu id labelled for Ethernet (`60MHz (Ethernet,
    no USB)`): V208 and V203RB's 10M MAC runs only at HCLK 60 / 120 MHz, and
    at the default clock ETH.begin() returns false at compile time, so an
    example built there would drop the whole stack and test nothing of it."""
    text = (REPO / "boards.txt").read_text(encoding="utf-8")
    return dict(re.findall(r"^([A-Za-z0-9_]+)\.menu\.clock\.(\w+)=[^\n]*\(Ethernet", text, re.M))


@functools.lru_cache(maxsize=None)
def part_limits(board: str, part: str) -> tuple:
    """(flash, RAM) of one part's menu entry."""
    text = (REPO / "boards.txt").read_text(encoding="utf-8")
    return tuple(int(re.search(rf"^{board}\.menu\.pnum\.{part}\.{key}=(\d+)$", text, re.M).group(1))
                 for key in ("upload.maximum_size", "upload.maximum_data_size"))


def unmet(need: dict, board: str, part: str = ""):
    """Why `board` (its ANY entry, or `part`) cannot run this sketch, in the
    sketch's own words, or None."""
    caps = set(series_capabilities().get(board, ()))
    if part and ethernet_parts().get(part) == board:
        caps.add("ETHERNET")
    missing = [c for c in need["caps"] if c not in caps]
    missing += [d for d in need.get("defines", ()) if d not in series_defines().get(board, ())]
    if missing:
        return f"requires {', '.join(missing)}, which {board} does not have"
    flash, ram = part_limits(board, part) if part else series_limits().get(board, (0, 0))
    where = part or f"{board} ANY"
    if need["flash"] and flash < need["flash"]:
        return (f"requires {need['flash'] // 1024} KB flash, "
                f"{where} has {flash // 1024} KB")
    if need["ram"] and ram < need["ram"]:
        return (f"requires {need['ram'] // 1024} KB RAM, "
                f"{where} has {ram // 1024} KB")
    return None
