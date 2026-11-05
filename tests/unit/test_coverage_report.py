"""tests/coverage.py: the coverage report from run_hardware.py's output (result.json, bench.json, junit files)."""
import importlib.util
import json
import pathlib

REPO = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("coverage_report", REPO / "tests/coverage.py")
coverage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(coverage)


def run_dir(tmp_path, target, chip, cases):
    """A run directory: cases is {case: {test name: "pass" | "fail" | "skip"}}."""
    run = tmp_path / f"run-{target}"
    (run / "cases" / target).mkdir(parents=True)
    (run / "bench.json").write_text(json.dumps({"targets": {target: {"chip": chip}}}))
    (run / "result.json").write_text(json.dumps({"cases": [dict(target=target, case=c, status="PASS") for c in cases]}))
    for case, tests in cases.items():
        d = run / "cases" / target / case
        d.mkdir()
        body = {"pass": "", "fail": "<failure message='x'/>", "skip": "<skipped message='no labels'/>"}
        rows = "".join(f'<testcase classname="" name="{n}">{body[o]}</testcase>' for n, o in tests.items())
        (d / "junit.xml").write_text(f"<testsuites><testsuite>{rows}</testsuite></testsuites>")
    return run


def test_series_and_once(tmp_path):
    a = run_dir(tmp_path, "x035-oep", "CH32X035C8T6",
                {"gpio": {"test_gpio_contract": "pass"}, "sd": {"test_sd_spi": "skip", "test_sd_sdio": "skip"},
                 "periph": {"test_planned[can]": "skip"}, "signals": {"test_pwm": "fail"}})
    b = run_dir(tmp_path, "v307-linke", "CH32V307VCT6", {"sd": {"test_sd_spi": "pass"}})
    caps = {s: set(c) for s, c in coverage.series_capabilities().items()}
    rows = coverage.collect([a, b / "result.json"], list(caps))
    text = coverage.report(rows, coverage.load_items(), caps)
    header = next(line for line in text.splitlines() if line.startswith("| series |"))
    cols = [c.strip() for c in header.strip("|").split("|")]
    x035 = [c.strip() for c in next(line for line in text.splitlines() if line.startswith("| CH32X035 |")).strip("|").split("|")]
    cell = dict(zip(cols, x035))
    assert cell["gpio"] == "✓" and cell["pwm"] == "✗" and cell["sd-sdio"] == "n/a" and cell["can"] == "n/a"
    assert cell["runtime"] == ""
    v003 = dict(zip(cols, [c.strip() for c in next(line for line in text.splitlines()
                                                     if line.startswith("| CH32V003 |")).strip("|").split("|")]))
    assert v003["uart2"] == "n/a"   # one USART
    assert "| sd-spi | ✓ | CH32V307 (v307-linke, run-v307-linke) |" in text


def test_every_item_names_a_case_run_hardware_knows():
    import sys
    sys.path.insert(0, str(REPO / "tests"))
    from harness.bench import CASES
    for item in coverage.load_items():
        assert item["scope"] in ("series", "once"), item
        assert set(item) <= {"id", "test", "scope", "needs", "any_of", "also", "not_on"}, item
        for test in coverage.tests_of(item):   # none: not covered yet, shown as "no test"
            assert test.partition("::")[0] in CASES, item


def test_a_list_of_tests_not_on_and_no_test(tmp_path):
    a = run_dir(tmp_path, "x035s3", "CH32X035F8U6", {"usb-peer": {"test_usb_peer": "pass"}})
    caps = {s: set(c) for s, c in coverage.series_capabilities().items()}
    text = coverage.report(coverage.collect([a], list(caps)), coverage.load_items(), caps)
    header = next(line for line in text.splitlines() if line.startswith("| series |"))
    cols = [c.strip() for c in header.strip("|").split("|")]

    def row(series):
        return dict(zip(cols, [c.strip() for c in next(line for line in text.splitlines()
                                                        if line.startswith(f"| {series} |")).strip("|").split("|")]))
    x035 = row("CH32X035")
    assert x035["usb-device"] == "✓"              # the S3 as the host counts for the device driver
    assert x035["wire-slave"] == "no test" and x035["interrupt"] == "" and x035["dac"] == "n/a"
    assert row("CH32V103")["usb-peer-host"] == "n/a" and row("CH32V307")["usb-peer-host"] == ""
    assert row("CH32M030")["watchdog"] == "n/a"
    assert "| console-sdi | not run |  |" in text and "| taskkit | not run |  |" in text and "| servo | not run |  |" in text
