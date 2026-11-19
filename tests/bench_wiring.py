"""The OEP lines each bench DUT should have, from what the tests look for (tests/BENCH-WIRING.ja.md).

For every bench part: the pads the probe labels now (read from the probes, lock-free, or from a saved snapshot),
and per test condition whether those lines meet it, else the pads to add. The conditions are the ones the
instrumented tests skip on (tests/instrumented/*: "no labelled line is ..."); the pads come from ch32-device-data's
pinout (timer / USART / SPI / I2C / ADC / DAC roles and routes), the variant's default pins (Wire, SPI, Serial) and
the part's pads to keep off (place.gpio_keep_off).

  uv run --project tests --extra hardware python tests/bench_wiring.py            # read the probes, write the doc
  uv run --project tests python tests/bench_wiring.py --labels saved.json         # from a snapshot
  ... --save-labels saved.json                                                     # keep what was read
"""
import argparse
import csv
import json
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tests"))
from harness import place  # noqa: E402

PINOUT = REPO / ".tools/ch32-device-data/index/pinout.csv"
OUT = REPO / "tests/BENCH-WIRING.ja.md"

# (probe unit, port) of each bench DUT; None: on a WCH-Link only (no OEP port yet).
BENCH = [
    ("CH32V003F4U6", "30eda0ea068b", "v003f4u6-uia"),
    ("CH32V003F4P6", "30eda0e34a0e", "v003f4p6"),
    ("CH32V006K8U6", "30eda0e343c6", "v006k8u6"),
    ("CH32V103R8T6", "80f1b2d80a34", "v103r8t6-evt"),
    ("CH32V203C8T6", "e8f60ae84e28", "v203c8t6"),
    ("CH32V205RCT6", "30eda0e31108", "v205rct6-pico"),
    ("CH32V208WBU6", "30eda0e31451", "v208wbu6"),
    ("CH32V307VCT6", None, None),
    ("CH32V407WEU6", "30eda0ea068b", "v407weu6-yd-p4"),
    ("CH32L103C8T6", "e8f60ae84dc6", "l103c8t6-pico"),
    ("CH32X035C8T6", "80f1b2d835d6", "x035c8t6-pico"),
    ("CH32X315MCU6", None, None),
    ("CH32H417QEU6", "e8f60ae84dc2", "h417qeu6-pico"),
]
UNITS = sorted({u for _, u, _ in BENCH if u})


def read_labels():
    from oep_client import link, settings
    out = {}
    for unit in UNITS:
        try:
            host = link.open_host(f"oep://{unit}", keep_session=False)
            try:
                out[unit] = settings.labels(host)
            finally:
                host.link.close()
        except Exception as e:      # an unreachable probe: its DUTs show as unwired
            print(f"oep://{unit}: {e}", file=sys.stderr)
    return out


RESERVED = ("swio", "swdio", "swclk", "nrst", "rx", "tx")


def port_pads(labels, unit, port):
    """{pad: role or None} of the port's test lines, its console pads (rx / tx), and {role: pad or ""} of its
    debug / reset / console labels (a label may name no pad: `<port>.nrst`)."""
    from_label = re.compile(r"^([^.@]+)(?:\.([^@]+))?(?:@(p[a-k]\d{1,2}))?$", re.I)
    pads, console, reserved = {}, set(), {}
    for _, text in labels.get(unit, []):
        m = from_label.match(text.strip())
        if not m or m.group(1).lower() != (port or "").lower():
            continue
        pad, role = (m.group(3) or "").upper(), (m.group(2) or "").lower() or None
        if role in RESERVED:
            reserved[role] = pad
            if role in ("rx", "tx") and pad:
                console.add(pad)
        elif not pad or role == "boot1" or (role or "").startswith("power"):
            continue
        else:
            pads[pad] = role
    return pads, console, reserved


def system_pads(part):
    """{"swdio" / "swclk" / "swio" / "nrst": pad} from device-data (NRST: "NRST" where it is no GPIO)."""
    out = {}
    with open(PINOUT, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            if r["part_number"] != part:
                continue
            pad = f"P{r['port']}{int(r['gpio'])}" if r["port"] and r["gpio"] else r["pad"]
            if r["peripheral"] == "SDI":
                out["swio" if r["signal"] == "SWIO" else r["role"].lower()] = pad
            elif r["peripheral"] == "SYS" and r["role"] == "NRST":
                out["nrst"] = pad
    if "swclk" not in out and "swdio" in out:
        out.setdefault("swio", out.pop("swdio"))
    return out


def pinout(part):
    """The part's rows, each pad as its GPIO name (PA0, not PA0-WKUP / OSC_IN); rows of no GPIO left out."""
    with open(PINOUT, newline="", encoding="utf-8") as f:
        rows = [r for r in csv.DictReader(f) if r["part_number"] == part and r["port"] and r["gpio"]]
    for r in rows:
        r["pad"] = f"P{r['port']}{int(r['gpio'])}"
    return rows


def variant_defaults(series):
    """Wire's SDA / SCL, SPI's SCK / MISO / MOSI, Serial's TX / RX: the variant's PIN_* (resolved)."""
    text = (REPO / "variants" / series / "pins_arduino.h").read_text()
    defs = dict(re.findall(r"^#define\s+(\w+)\s+(\S+)\s*$", text, re.M))

    def resolve(name):
        seen = 0
        while name in defs and seen < 8:
            name, seen = defs[name].strip("()"), seen + 1
        return name if re.fullmatch(r"P[A-K]\d{1,2}", name) else None
    return {k: resolve(k) for k in ("PIN_WIRE_SDA", "PIN_WIRE_SCL", "PIN_SPI_SCK", "PIN_SPI_MISO", "PIN_SPI_MOSI",
                                    "PIN_SERIAL_TX", "PIN_SERIAL_RX")}


def bit(pad):
    return int(pad[2:])


def plan(part, labels_now, console, reserved):
    rows = pinout(part)
    series = rows[0]["series"] if rows else part[:8]
    keep = set(place.gpio_keep_off(part) or []) | set(place.debug_pads(part) or [])
    d = variant_defaults(series)
    if not console:          # not wired yet: the console goes on Serial's default pads
        console = {p for p in (d["PIN_SERIAL_TX"], d["PIN_SERIAL_RX"]) if p}
    usable = lambda pad: pad not in keep and pad not in console           # noqa: E731
    have = set(labels_now)
    out = []          # (condition, tests, ok, lines to add, note): a line "PA8", or "PA13 (swdio)" with its role

    # The lines every DUT has (BENCH.ja.md §5): debug, reset, console. A role labelled on another pad (or on
    # none, as `.nrst`) counts.
    sysp = system_pads(part)
    debug_roles = ("swdio", "swclk") if "swclk" in sysp else ("swio",)
    tx, rx = d["PIN_SERIAL_TX"], d["PIN_SERIAL_RX"]
    for cond, tests, roles in (
            ("デバッグ（" + "・".join(r.upper() for r in debug_roles) + "）", "書き込み・デバッガ（run_hardware）",
             {r: sysp.get(r) for r in debug_roles}),
            ("リセット（NRST）", "書き込み、periph（リセットの理由）", {"nrst": sysp.get("nrst")}),
            ("コンソールの UART（rx は DUT の TX、tx は DUT の RX）", "すべての試験の READY と応答",
             {"rx": tx, "tx": rx})):
        missing = [f"{pad} ({role})" for role, pad in roles.items() if role not in reserved and pad]
        absent = [role for role, pad in roles.items() if role not in reserved and not pad]
        if "swdio" in roles and "swio" in reserved:
            # A two-wire chip on one wire: where two wires would not attach (V006, V205, H417 on OEP,
            # 2026-11-19), the probe keeps SWIO and the SWCLK pad stays a plain line.
            out.append((cond + "、いまは 1 線（SWIO）", tests, True, [], ""))
        elif not missing and not absent:
            out.append((cond, tests, True, [], ""))
        else:
            out.append((cond, tests, False, missing, ("端子が device-data に無い: " + ", ".join(absent)) if absent else ""))

    debug = set(place.debug_pads(part) or [])

    def need(cond, tests, choices, note="", fixed=False):
        """choices: candidate pad lists, best first; met when one is wholly labelled. fixed: pads the test cannot
        move (a peripheral's default pins), taken even where selfcheck keeps off (a USB pad the board leaves free)."""
        ok = (lambda p: p not in debug and p not in console) if fixed else usable
        given = [c for c in choices if c]
        choices = [c for c in given if all((ok(p) or p in have) and p not in console for p in c)]
        if given and not choices and not note:
            held = sorted({p for c in given for p in c if not ok(p) and p not in have})
            note = "候補は触らない端子かコンソールだけ: " + ", ".join(held)
        if any(set(c) <= have for c in choices):
            out.append((cond, tests, True, [], note))
        elif choices:
            best = min(choices, key=lambda c: len(set(c) - have))
            out.append((cond, tests, False, [p for p in best if p not in have], note))
        else:
            out.append((cond, tests, None, [], note or "この型番に条件を満たす端子が無い"))

    pads_all = sorted({r["pad"] for r in rows if re.fullmatch(r"P[A-K]\d{1,2}", r["pad"])})

    def need_plain(cond, tests, n, exclude=()):
        """n plain lines (any pads), first and last of them on different EXTI numbers."""
        mine = [p for p in sorted(have) if p not in exclude]
        if len(mine) >= n and len({bit(p) for p in mine}) > 1:
            out.append((cond, tests, True, [], ""))
            return
        extra = [p for p in pads_all if usable(p) and p not in have and p not in exclude
                 and bit(p) not in {bit(q) for q in mine}][:max(0, n - len(mine))]
        out.append((cond, tests, False, extra, ""))

    # Timers: role rows grouped by (timer, route).
    tim = {}
    for r in rows:
        if re.fullmatch(r"TIM\d+", r["peripheral"]):
            tim.setdefault((r["peripheral"], r["route"]), {}).setdefault(r["role"], r["pad"])
    two = [[g["CH1"], g["CH2"]] for g in tim.values() if "CH1" in g and "CH2" in g]
    pair = [[g[f"CH{c}"], g[f"CH{c}N"], g["BKIN"]] for g in tim.values() for c in (1, 2, 3)
            if f"CH{c}" in g and f"CH{c}N" in g and "BKIN" in g]
    six = [[g[k] for k in ("CH1", "CH1N", "CH2", "CH2N", "CH3", "CH3N")] for g in tim.values()
           if all(k in g for k in ("CH1", "CH1N", "CH2", "CH2N", "CH3", "CH3N"))]
    only_n = sorted({p for g in tim.values() for k, p in g.items() if re.fullmatch(r"CH\dN", k)} -
                    {p for g in tim.values() for k, p in g.items() if re.fullmatch(r"CH\d", k)})
    remap_only = sorted({p for (t, route), g in tim.items() if route != "default" for k, p in g.items()
                         if re.fullmatch(r"CH\d", k)} -
                        {p for (t, route), g in tim.items() if route == "default" for p in g.values()})

    need_plain("GPIO の線 2 本", "gpio、periph（sleep / watchdog）、SoftwareSerial", 2)
    need("タイマーの CH1 か CH2", "signals（PWM・捕捉・ワンパルスのトリガ・timerPulseIn）",
         [[p] for g in tim.values() for k, p in g.items() if k in ("CH1", "CH2")])
    need("1 つのタイマー・経路の CH1 と CH2", "signals::test_pwm_timer、test_phase_shift", two)
    need("CHx・CHxN・BKIN（同じ経路）", "signals::test_complementary（BKIN でブレーキも）", pair)
    need("CH1〜CH3 と CH1N〜CH3N（同じ経路）", "signals::test_six_step", six)
    need("CHxN だけの端子（analogWrite() が CHxN を取る）", "signals::test_pwm_complementary", [[p] for p in only_n])
    need("remap 先にだけあるタイマーの端子", "signals::test_pwm_route", [[p] for p in remap_only])

    # ADC: a channel, and the external trigger pad with another channel.
    adc = sorted({r["pad"] for r in rows if r["peripheral"].startswith("ADC") and re.fullmatch(r"IN\d+", r["role"])})
    retr = sorted({r["pad"] for r in rows if r["peripheral"].startswith("ADC") and r["role"] in ("RETR", "IETR")})
    need("ADC のチャネル", "periph（analogRead・ストリーム・ウォッチドッグ）、adc", [[p] for p in adc])
    need("ADC の外部トリガの端子と別のチャネル", "adc（beginExternal）", [[t, a] for t in retr for a in adc if a != t])
    dac = sorted({r["pad"] for r in rows if r["peripheral"].startswith("DAC") and "OUT" in r["role"]})
    if dac:
        need("DAC の出力", "periph（analogWrite の DAC）", [[p] for p in dac])

    # Serial: a second USART route (both pads), the console USART's CTS / RTS.
    usart = {}
    for r in rows:
        if re.fullmatch(r"U(S)?ART\d+", r["peripheral"]):
            usart.setdefault((r["peripheral"], r["route"]), {}).setdefault(r["role"], r["pad"])
    cons_usart = {u for (u, _), g in usart.items() if console and {g.get("TX"), g.get("RX")} & console}
    need("2 本目の USART（TX と RX）", "uart（Serial2 など）、IrDA、RS485 の DE と合わせて",
         [[g["TX"], g["RX"]] for (u, _), g in usart.items() if "TX" in g and "RX" in g and u not in cons_usart])
    need("コンソールの USART の CTS か RTS", "uart::test_uart_flow",
         [[g[k]] for (u, _), g in usart.items() if u in cons_usart for k in ("CTS", "RTS") if k in g])

    # I2C, SPI: the variant's defaults; SoftWire / SoftSPI: plain lines with distinct bits.
    sda, scl = d["PIN_WIRE_SDA"], d["PIN_WIRE_SCL"]
    if sda and scl:
        need("Wire の既定の SDA・SCL", "wire", [[sda, scl]], fixed=True)
    need_plain("SoftWire の 2 本（Wire の端子以外、EXTI の番号が別）", "softwire", 2, exclude=(sda, scl))
    sck, miso, mosi = d["PIN_SPI_SCK"], d["PIN_SPI_MISO"], d["PIN_SPI_MOSI"]
    if sck and miso and mosi:
        nss = [r["pad"] for r in rows if r["peripheral"] == "SPI1" and r["role"] == "NSS"
               and any(x["pad"] == sck and x["route"] == r["route"] and x["role"] == "SCK" for x in rows)]
        need("SPI の既定の SCK・MISO・MOSI と CS（NSS の端子なら setHwCs も）", "spi（transferAsync・setHwCs）",
             [[sck, miso, mosi, n] for n in nss] + [[sck, miso, mosi]], fixed=True)
    need_plain("SoftSPI の 4 本（SCK と CS の EXTI の番号が別）", "softspi", 4)

    if series in ("CH32V407", "CH32V467", "CH32X305", "CH32X315"):
        text = (REPO / "variants" / series / "pins_arduino.h").read_text()
        argb = sorted(set(re.findall(r"\(p\) == (P[A-K]\d+)", text.split("CH32RV_ARGB_PIN_TO_AF")[1].split("\n\n")[0]))) \
            if "CH32RV_ARGB_PIN_TO_AF" in text else []
        need("WS2812 ブロック（ARGB）の端子", "signals::test_rgb_led_hardware", [[p] for p in argb])
    if series in ("CH32H417", "CH32H416", "CH32H415"):
        need("HSADC の入力（PF8〜PF10、VDDIO）", "hsadc", [["PF8"], ["PF9"], ["PF10"]])
    return series, keep, out, console


def render(results, labels_known):
    lines = ["# OEP の配線の望ましい形（ファミリーごと）", "",
             "この文書は `tests/bench_wiring.py` が書きます（手で直さない）。試験がスキップする条件（「ラベルの付いた線が無い」",
             "など）を、いまプローブに付いているラベルと、ch32-device-data の端子表（タイマー・USART・SPI・I2C・ADC・DAC の",
             "役割と経路）、variant の既定の端子、型番の触らない端子（水晶・USB・デバッグなど）から突き合わせています。",
             "",
             "- 線はすべて `<口>@<端子>`（役割の要らない普通の線）で足せます。役割のある線（SD、CAN など）は",
             "  [BENCH.ja.md](BENCH.ja.md) §5 の表です。配線の規則（RVSWD・UART・NRST は + 近く、GND 固定の BOOT は触らない）も",
             "  そちらに従います。",
             "- 「足す端子」は条件ごとの最小の候補で、ほかの条件の候補と重なるものは 1 本で両方を満たします。台ごとに",
             "  重ねずにまとめた一覧が先頭の「足す線の一覧」です。",
             "- デバッグ（2 線の系列は SWDIO・SWCLK、V003 は SWIO）・リセット（NRST）・コンソールの線は、役割のラベル",
             "  （`.swdio@<端子>` など）で付いていれば済とします（NRST は端子の書かれていない `.nrst` でも）。2 線の系列でも",
             "  1 線（`.swio@`）で付いていれば済です（V006・V205・H417 は OEP から 2 線で接続できず 1 線、2026-11-19）。",
             "- OPA・コンパレータ・CAN のループバックは DUT の自己試験なので線は要りません（端子が触らない端子でなければ動く）。",
             "- 1 台の P4 で約 16 本が目安（BENCH.ja.md §5）。足りないときは、上の行（多くの試験が使う条件）から引きます。",
             ""]
    if not labels_known:
        lines += ["> プローブのラベルを読めなかったので、すべて未配線として計算しています。", ""]
    def key(line):
        """Port and bit order; a pad of no GPIO (NRST) last."""
        pad = line.split()[0]
        return (0, pad[1], int(pad[2:])) if re.fullmatch(r"P[A-K]\d{1,2}", pad) else (1, pad, 0)

    def label(port, line):
        """The probe label for the line: `<port>@<pad>`, or `<port>.<role>@<pad>` (a pad of no GPIO: `<port>.<role>`)."""
        pad, _, role = line.partition(" (")
        role = role.rstrip(")")
        tail = f"@{pad.lower()}" if re.fullmatch(r"P[A-K]\d{1,2}", pad) else ""
        return f"`{port or '<口>'}{'.' + role if role else ''}{tail}`"

    summary = ["## 足す線の一覧（端子順）", "",
               "条件ごとの表の「足す端子」を台ごとに重ねずにまとめ、端子の順に並べたものです。ラベルの形で書いています",
               "（役割の付く線はデバッグ・リセット・コンソールだけ。ほかは普通の線）。", "",
               "「ラベルを直す線」は、線はもう引いてあって役割のラベル（`.swclk@` など）が付いていないものです。",
               "",
               "| 台 | 足す線 | ラベルを直す線 |", "|---|---|---|"]
    sections = []
    for part, unit, port, series, now, console, keep, rows, reserved in results:
        every = sorted({line for _, _, _, pads, _ in rows for line in pads}, key=key)
        wired = set(now) | {p for p in reserved.values() if p}       # pads a line reaches now, any label
        relabel = [x for x in every if " (" in x and x.split()[0] in wired]
        todo = [x for x in every if x not in relabel]
        summary.append(f"| {part} | {len(todo)} 本: {', '.join(label(port, x) for x in todo) or 'なし'} | "
                       f"{', '.join(label(port, x) for x in relabel) or 'なし'} |")
        where = f"`oep://{unit}/{port}`" if unit else "OEP 未接続（WCH-Link のみ）"
        sections += [f"## {part}（{series}）", "", f"- 口: {where}"]
        sections.append(f"- いまの線: {', '.join(sorted(now, key=key)) or 'なし'}")
        sections.append(f"- コンソール（rx / tx）: {', '.join(sorted(console, key=key)) or '不明'}" +
                        ("" if now else "（未配線: Serial の既定の端子）"))
        sections.append(f"- 触らない端子: {', '.join(sorted(keep, key=key)) or 'なし'}")
        sections.append(f"- 足す線（{len(todo)} 本、端子順）: " + (", ".join(label(port, x) for x in todo) or "なし"))
        if relabel:
            sections.append("- ラベルを直す線: " + ", ".join(label(port, x) for x in relabel))
        sections += ["", "| 条件 | 使う試験 | いま | 足す端子 |", "|---|---|---|---|"]
        for cond, tests, ok, pads, note in rows:
            state = "済" if ok else ("該当なし" if ok is None else "未")
            cell = ", ".join(pads) or (note if ok is None or not ok else "")
            sections.append(f"| {cond} | {tests} | {state} | {cell} |")
        sections.append("")
    lines += summary + [""] + sections
    lines += ["## M030", "",
              "CH32M030G8R7 の板はモーター基板で、端子を出力にしません（defect m030-motor-stage）。GPIO を動かす試験の配線は",
              "しません。", ""]
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--labels", help="a snapshot of the probes' labels (JSON) instead of reading them")
    ap.add_argument("--save-labels", help="write the labels read to this JSON")
    ap.add_argument("--out", default=str(OUT))
    a = ap.parse_args()
    if a.labels:
        labels = json.loads(pathlib.Path(a.labels).read_text())
    else:
        labels = read_labels()
    if a.save_labels:
        pathlib.Path(a.save_labels).write_text(json.dumps(labels))
    results = []
    for part, unit, port in BENCH:
        now, console, reserved = port_pads(labels, unit, port) if unit else ({}, set(), {})
        series, keep, rows, console = plan(part, now, console, reserved)
        results.append((part, unit, port, series, now, console, keep, rows, reserved))
    pathlib.Path(a.out).write_text(render(results, bool(labels)) + "\n")
    print(f"wrote {a.out}")


if __name__ == "__main__":
    main()
