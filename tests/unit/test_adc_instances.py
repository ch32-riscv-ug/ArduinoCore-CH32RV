"""Every analog pad a variant names can actually be converted.

analogRead() takes the channel from CH32_PIN_TO_ADC_CHANNEL and, where the
series has several ADCs on disjoint pads, the instance from
CH32_PIN_TO_ADC_INSTANCE. A pad that is in one macro and not the other is the
failure this guards: analogRead() returns 0 for a pad the header advertises,
and nothing else notices, because a reading of 0 looks like a grounded input.

Only CH32X305 and CH32X315 have disjoint instances. Everywhere else the extra
ADCs sit on the same pads as ADC1, so the variant emits no instance macro and
wiring_analog.c keeps its single-instance path - which is asserted here too,
because that is what keeps the 16 KB parts from paying for this.
"""
import pathlib
import re

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
VARIANTS = sorted((REPO / "variants").glob("*/pins_arduino.h"))

# The series whose ADCs are on disjoint pads, with a pad from each instance.
# Values are read off ch32-device-data's pin_functions.csv, whose CH32X315 ADC
# base addresses are `confirmed` against the reference manual.
DISJOINT = {
    "CH32X305": {"PA2": (1, 0), "PB6": (2, 0), "PB10": (3, 0), "PC0": (4, 2)},
    "CH32X315": {"PA2": (1, 0), "PB6": (2, 0), "PB10": (3, 0), "PD14": (4, 0)},
}


def _macro_pairs(text: str, macro: str) -> dict:
    """{pad: value} out of a generated `(p) == PA1 ? 3 : \\` ladder."""
    body = text.split(f"#define {macro}(p)", 1)
    if len(body) == 1:
        return {}
    body = re.split(r"^#define ", body[1], maxsplit=1, flags=re.M)[0]
    return {pad: int(value)
            for pad, value in re.findall(r"\(p\) == (P[A-F]\d+) \? (\d+)", body)}


def _aliases(text: str) -> dict:
    return {int(n): pad
            for n, pad in re.findall(r"^#define A(\d+)\s+(P[A-F]\d+)", text, re.M)}


def test_the_disjoint_series_are_the_only_ones():
    """A third one appearing is a decision, not a silent generator change."""
    found = {h.parent.name for h in VARIANTS
             if "CH32_ADC_INSTANCE_COUNT" in h.read_text(encoding="utf-8")}
    assert found == set(DISJOINT), (
        f"series with several ADCs on disjoint pads changed: {sorted(found)}; "
        f"wiring_analog.c's single-instance path covers the rest")


@pytest.mark.parametrize("header", VARIANTS, ids=lambda p: p.parent.name)
def test_every_instance_pad_has_a_channel(header):
    text = header.read_text(encoding="utf-8")
    channels = _macro_pairs(text, "CH32_PIN_TO_ADC_CHANNEL")
    instances = _macro_pairs(text, "CH32_PIN_TO_ADC_INSTANCE")
    orphans = sorted(set(instances) - set(channels))
    assert not orphans, (
        f"{header.parent.name} gives {orphans} an ADC instance but no channel, "
        f"so analogRead() returns 0 for them")


@pytest.mark.parametrize("header", VARIANTS, ids=lambda p: p.parent.name)
def test_the_instance_macro_travels_with_the_count(header):
    text = header.read_text(encoding="utf-8")
    has_count = "CH32_ADC_INSTANCE_COUNT" in text
    has_macro = "CH32_PIN_TO_ADC_INSTANCE(p)" in text
    assert has_count == has_macro, (
        f"{header.parent.name}: CH32_ADC_INSTANCE_COUNT and "
        f"CH32_PIN_TO_ADC_INSTANCE must be emitted together")
    if not has_count:
        return
    count = int(re.search(r"#define CH32_ADC_INSTANCE_COUNT (\d+)", text).group(1))
    table = re.search(r"#define CH32_ADC_INSTANCES \{(.*?)\n    \}", text, re.S)
    assert table, f"{header.parent.name}: no CH32_ADC_INSTANCES table"
    rows = re.findall(r"\{ CH32_ADC\d+_BASE,", table.group(1))
    assert len(rows) == count, (
        f"{header.parent.name}: CH32_ADC_INSTANCE_COUNT is {count} but the "
        f"table has {len(rows)} rows")


@pytest.mark.parametrize("header", VARIANTS, ids=lambda p: p.parent.name)
def test_the_a_aliases_stay_on_adc1(header):
    """A<n> is ADC1's numbering. If an alias moved, analogRead(A0) changed pad."""
    text = header.read_text(encoding="utf-8")
    instances = _macro_pairs(text, "CH32_PIN_TO_ADC_INSTANCE")
    if not instances:
        return
    wrong = {f"A{n}": pad for n, pad in _aliases(text).items()
             if instances.get(pad, 1) != 1}
    assert not wrong, (
        f"{header.parent.name}: {wrong} are named A<n> but sit on another ADC")


@pytest.mark.parametrize("series", sorted(DISJOINT))
def test_the_disjoint_pads_map_where_device_data_says(series):
    text = (REPO / "variants" / series / "pins_arduino.h").read_text(
        encoding="utf-8")
    channels = _macro_pairs(text, "CH32_PIN_TO_ADC_CHANNEL")
    instances = _macro_pairs(text, "CH32_PIN_TO_ADC_INSTANCE")
    for pad, (want_instance, want_channel) in DISJOINT[series].items():
        assert instances.get(pad, 1) == want_instance, f"{pad} instance"
        assert channels.get(pad) == want_channel, f"{pad} channel"
