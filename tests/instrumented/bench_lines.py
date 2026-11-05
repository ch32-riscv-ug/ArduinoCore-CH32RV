"""The bench's lines, from the OEP probe's labels (oep host guide §5.3) - the only record of the wiring (the bench
file's gpio table, soft_spi, soft_wire and spi_cs are gone, 2026-11-04):

  <port>@<pad>         a line from the probe to DUT pad <pad>, for any test (gpio drives it both ways)
  <port>.<role>@<pad>  the same line, which a test uses as <role> (ROLES; tests/BENCH.ja.md §5 lists them)
  <port>.<role>        debug, reset, console, power (swio, swdio, swclk, nrst, rx, tx, power_*): not test lines

A test asks for a feature's lines and skips when the probe does not label all the required ones: wiring a feature
on the bench is labelling it, and nothing else."""
import re

import pytest

# feature: (required roles, optional roles). Optional roles widen the test (sdio's d1-d3: 4 lines instead of 1).
ROLES = {
    # lines between the DUT and the probe only
    # lines to a device on the bench (each also on a probe channel, which stays an input): DEVICE_FEATURES
    # storage (libraries/SD, libraries/SD_MMC): a card wired to the DUT
    "spisd": (("spisd_sck", "spisd_miso", "spisd_mosi", "spisd_cs"), ()),
    "sdio": (("sdio_ck", "sdio_cmd", "sdio_d0"), ("sdio_d1", "sdio_d2", "sdio_d3")),
    # not implemented yet; the names are fixed now so the wiring can come first
    "can": (("can_tx", "can_rx"), ()),
    "i2s": (("i2s_ck", "i2s_ws"), ("i2s_sdo", "i2s_sdi", "i2s_mck")),
    "argb": (("argb",), ()),
    "rs485": (("rs485_tx", "rs485_rx", "rs485_de"), ("rs485_re",)),
    "qspi": (("qspi_sck", "qspi_cs", "qspi_io0", "qspi_io1"), ("qspi_io2", "qspi_io3")),
}


def port_of(target):
    """The probe port's name S of the target (its upload_port oep://<unit>/<S>), or None."""
    up = target.get("upload_port", "")
    if not up.startswith("oep://"):
        return None
    parts = up[len("oep://"):].split("/")
    return parts[1] if len(parts) > 1 and parts[1] else None


def pin_number(pad):
    """The sketch's pin number of a pad ("pa7" / "PA7"): the variants' CH32RV_PIN(port, bit)."""
    pad = pad.upper()
    return ((ord(pad[1]) - ord("A") + 2) << 5) | int(pad[2:])


def lines(oep_host, target, feature):
    """{role: {"channel": n, "pad": "PA7", "pin": number}} for the feature's labelled roles, and the missing
    required roles. Labels without a pad (`S.role`) do not count: the test needs the DUT pad."""
    from oep_client import settings
    required, optional = ROLES[feature]
    port = port_of(target)
    found = {}
    host = getattr(oep_host, "host", oep_host)   # the oep_host fixture, or a Host
    if port is not None and host is not None:
        for channel, text in settings.labels(host):
            parsed = settings.parse_label(text)
            if not parsed or settings.fold_name(parsed[0]) != settings.fold_name(port) or not parsed[1] or not parsed[2]:
                continue
            role = settings.fold_name(parsed[1])
            if (role in required or role in optional) and re.fullmatch(r"p[a-k]\d{1,2}", parsed[2], re.I):
                pad = parsed[2].upper()
                found[role] = dict(channel=channel, pad=pad, pin=pin_number(pad))
    missing = [r for r in required if r not in found]
    return found, missing


def require(oep_host, target, feature):
    """The feature's lines, or skip the test naming the labels that would wire it."""
    found, missing = lines(oep_host, target, feature)
    if missing:
        port = port_of(target) or "<port>"
        pytest.skip(f"{feature}: the probe labels no " + ", ".join(f"{port}.{r}@<pad>" for r in missing))
    return found


# Features whose lines reach a device besides the probe: the gpio test never drives them.
DEVICE_FEATURES = {"spisd", "sdio", "can", "i2s", "rs485", "qspi", "argb"}
# Port lines that are not test lines (debug, reset, console, power).
RESERVED = {"swio", "swdio", "swclk", "nrst", "rx", "tx"}


def port_lines(oep_host, target):
    """Every labelled line of the target's port that reaches a pad: [{"channel", "pad", "pin", "role", "label"}], role
    None for a plain `<port>@<pad>`; label is the probe's label text (what oep_client.acquire names a line by)."""
    from oep_client import settings
    port = port_of(target)
    host = getattr(oep_host, "host", oep_host)
    out = []
    if port is None or host is None:
        return out
    for channel, text in settings.labels(host):
        parsed = settings.parse_label(text)
        if parsed and settings.fold_name(parsed[0]) == settings.fold_name(port) and parsed[2] \
                and re.fullmatch(r"p[a-k]\d{1,2}", parsed[2], re.I):   # one pad (not "pc16-pc11")
            pad = parsed[2].upper()
            role = settings.fold_name(parsed[1]) if parsed[1] else None
            out.append(dict(channel=channel, pad=pad, pin=pin_number(pad), role=role, label=text))
    return sorted(out, key=lambda line: line["channel"])


def gpio_lines(oep_host, target):
    """The lines the gpio test may drive both ways: plain ones and those of probe-only features."""
    device_roles = {r for f in DEVICE_FEATURES for group in ROLES[f] for r in group}
    return [line for line in port_lines(oep_host, target)
            if line["role"] is None or (line["role"] not in RESERVED and line["role"] not in device_roles
                                        and not line["role"].startswith("power"))]


def channels(oep_host, target):
    """pad -> probe channel, over every labelled line of the port."""
    return {line["pad"]: line["channel"] for line in port_lines(oep_host, target)}
