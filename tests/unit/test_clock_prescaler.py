"""The AHB prescaler table in cores/arduino/ch32_clock.h.

F_CPU is the target HCLK and the prescaler is derived from it, so that a
different clock is a boards.txt change and nothing else. That only holds if the
derivation is right, and it is easy to get wrong: the CH32 line encodes the
four-bit prescaler field two different ways, agreeing only on /1. Read 0x1 as
/2 where it means /1 and the part runs at twice the clock every timing
calculation assumes - Serial comes out garbled and nothing says why.

So the table is checked against both encodings here, at compile time and
without a board: a _Static_assert on the field the header picks, plus the
ratios each encoding cannot express, which have to be #errors rather than
silently wrong numbers.

Values come from each family's own EVT header (RCC_HPRE_DIVn); the mapping from
family to encoding lives in tools/generate/generate.py.
"""
import subprocess

import pytest

# (linear?, HSI, F_CPU, field) - the field is what RCC_CFGR0[7:4] must become.
#
# linear   0x0..0x7 = /1../8, then 0xB /16  0xC /32  0xD /64  0xE /128  0xF /256
# pow2     0x0 /1  0x8 /2  0x9 /4  0xA /8  0xB /16  0xC /64  0xD /128
#          0xE /256  0xF /512                                  - and no /32
ENCODES = [
    # linear: V00x, M030, X03x. /3, /5, /6 and /7 exist only here.
    (1, 48_000_000, 48_000_000, 0x0),
    (1, 48_000_000, 24_000_000, 0x1),
    (1, 48_000_000, 16_000_000, 0x2),
    (1, 48_000_000, 12_000_000, 0x3),
    (1, 48_000_000, 8_000_000, 0x5),
    (1, 48_000_000, 6_000_000, 0x7),
    (1, 48_000_000, 3_000_000, 0xB),
    (1, 48_000_000, 1_500_000, 0xC),      # /32, which the other encoding lacks
    (1, 48_000_000, 750_000, 0xD),
    (1, 24_000_000, 24_000_000, 0x0),
    (1, 24_000_000, 8_000_000, 0x2),
    # pow2: V10x, V20x, V30x, V4x7, L103, V205, X3x5
    (0, 8_000_000, 8_000_000, 0x0),
    (0, 8_000_000, 4_000_000, 0x8),
    (0, 8_000_000, 2_000_000, 0x9),
    (0, 8_000_000, 1_000_000, 0xA),
    (0, 8_000_000, 500_000, 0xB),
    (0, 8_000_000, 125_000, 0xC),         # /64 - note 0xC is /32 on the other
    (0, 20_000_000, 20_000_000, 0x0),
    (0, 20_000_000, 5_000_000, 0x9),
]

# (linear?, HSI, F_CPU, what the message should mention)
REFUSES = [
    (1, 48_000_000, 96_000_000, "above SYSCLK"),  # more than the clock produces
    (0, 8_000_000, 16_000_000, "above SYSCLK"),
    (1, 48_000_000, 5_000_000, "divide"),         # not a whole ratio
    (0, 20_000_000, 3_000_000, "divide"),
    (1, 48_000_000, 2_000_000, "prescaler"),      # /24: no encoding has it
    (0, 8_000_000, 250_000, "prescaler"),         # /32: the pow2 gap
    (0, 8_000_000, 8_000, "prescaler"),           # /1000
]


def compile_probe(gcc_bin, repo, tmp_path, linear, hsi, f_cpu, expect=None,
                  sysclk=None):
    """Compile a TU that asserts the field, and return (rc, output).

    sysclk defaults to the oscillator, which is the no-PLL case. Passing a
    different one is what a PLL configuration looks like to this header: the
    prescaler divides SYSCLK, not the oscillator.
    """
    src = tmp_path / "probe.c"
    body = f"#include <ch32_clock.h>\n"
    if expect is not None:
        body += (f"_Static_assert(CH32_HPRE_FIELD == {expect}u,\n"
                 f'               "wrong AHB prescaler field");\n')
    src.write_text(body, encoding="utf-8")
    sysclk = hsi if sysclk is None else sysclk
    proc = subprocess.run(
        [f"{gcc_bin}/riscv-none-elf-gcc", "-std=c11", "-fsyntax-only",
         f"-I{repo / 'cores' / 'arduino'}",
         f"-DF_CPU={f_cpu}L", f"-DCH32_HSI_HZ={hsi}",
         f"-DCH32_CLOCK_SYSCLK_HZ={sysclk}",
         f"-DCH32_CLOCK_USE_PLL={0 if sysclk == hsi else 1}",
         f"-DCH32_HPRE_LINEAR={linear}", str(src)],
        capture_output=True, text=True)
    return proc.returncode, proc.stdout + proc.stderr


@pytest.mark.parametrize("linear,hsi,f_cpu,field", ENCODES)
def test_ratio_picks_the_right_field(gcc_bin, repo, tmp_path, linear, hsi,
                                     f_cpu, field):
    """The _Static_assert is the test: a wrong field is a compile error."""
    rc, out = compile_probe(gcc_bin, repo, tmp_path, linear, hsi, f_cpu,
                            expect=field)
    assert rc == 0, out


@pytest.mark.parametrize("linear,hsi,f_cpu,mentions", REFUSES)
def test_impossible_ratio_is_refused(gcc_bin, repo, tmp_path, linear, hsi,
                                     f_cpu, mentions):
    """And a ratio the hardware cannot reach has to say so, not round quietly."""
    rc, out = compile_probe(gcc_bin, repo, tmp_path, linear, hsi, f_cpu)
    assert rc != 0, f"F_CPU={f_cpu} off a {hsi} Hz oscillator should not compile"
    assert mentions in out, out


def test_every_family_declares_an_encoding(repo):
    """No family may reach the core without one - the default would be a guess."""
    boards = (repo / "boards.txt").read_text(encoding="utf-8")
    families = [line.split(".")[0] for line in boards.splitlines()
                if ".build.core_defines=" in line]
    without = [f for f, line in zip(families,
                                    [ln for ln in boards.splitlines()
                                     if ".build.core_defines=" in ln])
               if "-DCH32_HPRE_LINEAR=" not in line]
    assert families and not without, f"{without} carry no CH32_HPRE_LINEAR"


# The PLL only moves what the prescaler divides, so the same table has to hold
# with SYSCLK above the oscillator - and F_CPU above SYSCLK must still refuse.
PLL_CASES = [
    # (linear, hsi, sysclk, f_cpu, field)
    (0, 8_000_000, 144_000_000, 144_000_000, 0x0),   # CH32V20x / CH32V30x
    (0, 8_000_000, 144_000_000, 72_000_000, 0x8),
    (0, 8_000_000, 144_000_000, 18_000_000, 0xA),
    (1, 24_000_000, 48_000_000, 48_000_000, 0x0),    # CH32V003
    (1, 24_000_000, 48_000_000, 16_000_000, 0x2),
]


@pytest.mark.parametrize("linear,hsi,sysclk,f_cpu,field", PLL_CASES)
def test_prescaler_divides_sysclk_not_the_oscillator(gcc_bin, repo, tmp_path,
                                                     linear, hsi, sysclk,
                                                     f_cpu, field):
    rc, out = compile_probe(gcc_bin, repo, tmp_path, linear, hsi, f_cpu,
                            expect=field, sysclk=sysclk)
    assert rc == 0, out


def test_f_cpu_above_sysclk_is_refused(gcc_bin, repo, tmp_path):
    """The prescaler can only divide; asking for more than the PLL produces is
    a mistake worth catching at compile time rather than at 2x the clock."""
    rc, out = compile_probe(gcc_bin, repo, tmp_path, 0, 8_000_000, 200_000_000,
                            sysclk=144_000_000)
    assert rc != 0, out
    assert "F_CPU is above SYSCLK" in out, out


def test_sysclk_above_the_oscillator_needs_a_pll_setting(gcc_bin, repo,
                                                         tmp_path):
    """SYSCLK and the PLL setting are generated together, so one without the
    other means boards.txt was hand-edited or half-regenerated."""
    src = tmp_path / "probe.c"
    src.write_text("#include <ch32_clock.h>\n", encoding="utf-8")
    proc = subprocess.run(
        [f"{gcc_bin}/riscv-none-elf-gcc", "-std=c11", "-fsyntax-only",
         f"-I{repo / 'cores' / 'arduino'}",
         "-DF_CPU=144000000L", "-DCH32_HSI_HZ=8000000",
         "-DCH32_CLOCK_SYSCLK_HZ=144000000", "-DCH32_CLOCK_USE_PLL=0",
         "-DCH32_HPRE_LINEAR=0", str(src)],
        capture_output=True, text=True)
    out = proc.stdout + proc.stderr
    assert proc.returncode != 0, out
    assert "no PLL setting" in out, out
