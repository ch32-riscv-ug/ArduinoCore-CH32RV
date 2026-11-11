"""The core's strftime(), tzset() and gmtime_r() (cores/arduino/ch32rv_strftime.c, ch32rv_tzset.c,
ch32rv_tzvars.c, ch32rv_gmtime.c), on the host.

format/time_cases.c checks strftime against newlib 4.5's text for every conversion, flag and width it takes
(the strings were printed by newlib's own strftime.c), the size limit, tzset against what newlib's
tzset_r.c makes of 47 TZ strings (malformed ones among them), and gmtime_r against the host's gmtime_r over
3 million times. The core's functions that the host also has are built under other names (core_strftime,
core_gmtime_r) so the host's stay in place; the time zone is the core's own (internal/ch32rv_tz.h).
"""
import pathlib
import shutil
import subprocess

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
CORE = REPO / "cores/arduino"
HERE = pathlib.Path(__file__).with_name("format")


def test_strftime_and_gmtime(tmp_path):
    cc = shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no host C compiler")
    base = [cc, "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", f"-I{CORE}"]
    objects = []
    for source, defines in [("ch32rv_strftime.c", ["-Dstrftime=core_strftime"]),
                            ("ch32rv_gmtime.c", ["-Dgmtime_r=core_gmtime_r", "-Dgmtime=core_gmtime"]),
                            ("ch32rv_tzset.c", ["-Dtzset=core_tzset_public"]),
                            ("ch32rv_tzvars.c", []),
                            ("ch32rv_format.c", [])]:
        obj = tmp_path / (source + ".o")
        subprocess.run([*base, *defines, "-c", str(CORE / source), "-o", str(obj)], check=True)
        objects.append(str(obj))
    exe = tmp_path / "time_cases"
    subprocess.run([*base, str(HERE / "time_cases.c"), *objects, "-o", str(exe)], check=True)
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout
    assert "failures 0" in run.stdout


def test_localtime_and_mktime(tmp_path):
    """format/tz_cases.c: localtime_r() and mktime() on the core's time zone against newlib 4.5's own."""
    cc = shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no host C compiler")
    base = [cc, "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", f"-I{CORE}"]
    objects = []
    for source, defines in [("ch32rv_localtime.c", ["-Dlocaltime_r=core_localtime_r", "-Dlocaltime=core_localtime",
                                                    "-Dgmtime_r=core_gmtime_r"]),
                            ("ch32rv_mktime.c", ["-Dmktime=core_mktime"]),
                            ("ch32rv_gmtime.c", ["-Dgmtime_r=core_gmtime_r", "-Dgmtime=core_gmtime"]),
                            ("ch32rv_tzset.c", ["-Dtzset=core_tzset_public"]),
                            ("ch32rv_tzvars.c", [])]:
        obj = tmp_path / (source + ".o")
        subprocess.run([*base, *defines, "-c", str(CORE / source), "-o", str(obj)], check=True)
        objects.append(str(obj))
    exe = tmp_path / "tz_cases"
    subprocess.run([*base, str(HERE / "tz_cases.c"), *objects, "-o", str(exe)], check=True)
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout
    assert "failures 0" in run.stdout
