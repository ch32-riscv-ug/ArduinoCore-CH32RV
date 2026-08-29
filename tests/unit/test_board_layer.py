"""The board layer rule holds in the tree, not just in the document.

docs/board-layer-rules.ja.md draws one line: a variant may define a name only
when its value follows from silicon (series x SKU). A name that is a claim
about how a *board* is wired belongs to a product board, and a Generic board -
which is a silicon series, not a PCB - must not invent one.

Rules that live only in a document come back. Each of these was true when it
was written and none of them was checked, so they are asserted here instead:
every one of them would otherwise regress as a silent success - a placeholder
pad that blinks nothing, or a register map that quietly enters every sketch.
"""
import pathlib
import re
import sys

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
CORE = REPO / "cores" / "arduino"
sys.path.insert(0, str(REPO / "tests"))

from sketch_requirements import BadRequirement, requirements   # noqa: E402

VARIANTS = sorted((REPO / "variants").glob("*/pins_arduino.h"))
EXAMPLES = sorted((REPO / "libraries").glob("*/examples/*/*.ino"))


def _strip_comments(text: str) -> str:
    """Code only. A header comment naming LED_BUILTIN is documentation."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def test_variants_are_present():
    """A glob that matched nothing would make every test below vacuous."""
    assert len(VARIANTS) >= 20, VARIANTS
    assert len(EXAMPLES) >= 20, EXAMPLES


@pytest.mark.parametrize("header", VARIANTS, ids=lambda p: p.parent.name)
def test_no_variant_defines_led_builtin(header):
    """LED_BUILTIN is a board's claim, and a Generic board is not a board.

    It used to be defined as the lowest-numbered pad common to every part in
    the series - a number with no meaning to the person reading it, on a pad
    with nothing attached. Only a product board, a sketch, or -DLED_BUILTIN on
    the command line may say where an LED is.
    """
    defined = re.findall(r"^\s*#\s*define\s+LED_BUILTIN\b",
                         header.read_text(encoding="utf-8"), re.M)
    assert not defined, (
        f"{header.relative_to(REPO)} defines LED_BUILTIN; see "
        f"docs/board-layer-rules.ja.md")


def _reachable_headers(variant: pathlib.Path) -> set:
    """Every quoted header a sketch pulls in through Arduino.h.

    Quoted includes only: <stdint.h> and friends are the toolchain's business.
    Conditional includes are followed unconditionally, which is the safe
    direction here - the question is whether a path exists at all.
    """
    roots = (CORE, variant.parent, CORE / "api")
    seen, stack = set(), [CORE / "Arduino.h"]
    while stack:
        cur = stack.pop()
        if cur in seen or not cur.is_file():
            continue
        seen.add(cur)
        for name in re.findall(r'^\s*#\s*include\s+"([^"]+)"',
                               cur.read_text(encoding="utf-8"), re.M):
            for root in (cur.parent,) + roots:
                target = (root / name)
                if target.is_file():
                    stack.append(target.resolve())
                    break
    return {p.name for p in seen}


@pytest.mark.parametrize("header", VARIANTS, ids=lambda p: p.parent.name)
def test_the_register_map_stays_out_of_the_sketch(header):
    """Arduino.h must not reach ch32_registers.h. `#include <CH32.h>` is the door.

    ch32_pins.h repeats CH32_GPIO_PORT_BASE rather than including the register
    map, and says so in a comment; wiring_digital.c has a _Static_assert so the
    two constants cannot drift. Nothing checked the include boundary itself,
    so one `#include "ch32_registers.h"` in ch32_pins.h would put 473 lines of
    register map into every sketch's namespace and no test would notice.
    """
    reachable = _reachable_headers(header)
    assert "Arduino.h" in reachable, "the walk found nothing; check the roots"
    assert "ch32_registers.h" not in reachable, (
        f"the register map reaches a sketch through Arduino.h on "
        f"{header.parent.name}; it belongs behind #include <CH32.h> "
        f"(docs/board-layer-rules.ja.md)")


@pytest.mark.parametrize("ino", EXAMPLES, ids=lambda p: p.stem)
def test_examples_guard_led_builtin(ino):
    """An example may use LED_BUILTIN, but never assume it exists.

    Guarded means `#ifdef LED_BUILTIN` (the LED is a nicety) or
    `#ifndef LED_BUILTIN` + `#error` (the LED is the point, as in Blink).
    Without a guard the example simply stops compiling for everyone.
    """
    code = _strip_comments(ino.read_text(encoding="utf-8"))
    if "LED_BUILTIN" not in code:
        return
    assert re.search(r"#\s*if(n?)def\s+LED_BUILTIN", code), (
        f"{ino.relative_to(REPO)} uses LED_BUILTIN without an "
        f"#ifdef/#ifndef guard")


@pytest.mark.parametrize("ino", EXAMPLES, ids=lambda p: p.stem)
def test_example_requirements_parse(ino):
    """A `requires:` line names real capabilities, or the sweep lies.

    An unknown name reads as "no series has this", which skips every board and
    leaves a green run that built nothing. sketch_requirements raises instead;
    this is what makes sure the raise is never reached in CI.
    """
    try:
        requirements(ino.parent)
    except BadRequirement as e:
        pytest.fail(str(e))
