# Test plan (entry points and how to run)

> Japanese: [TEST_PLAN.ja.md](TEST_PLAN.ja.md) is the reference; this is a summary.

The policy - layers, the bench definition, probe preparation, the automated / manual line and the
migration order - is in [docs/development-workflow.ja.md](../docs/development-workflow.ja.md).

Directories are cut by what a check **needs**, so each maps to one way of running it:

| directory | needs | run | CI |
|---|---|---|---|
| `unit/` | nothing | `uv run pytest unit` | every PR |
| `build/` | the toolchain, arduino-cli, device-data | `uv run pytest build` (`-m "not slow"` for seconds) | every PR |
| `bench/` | one permanent bench (probe + DUT behind `--profile`); builds the working tree (version-less profiles, the repo linked into the sketchbook, tools via `bench/install_tools.py`) | `uv run --env-file .env pytest bench --profile <board>` | self-hosted runner (later) |
| `manual/` | a person or outside equipment | name the file: `uv run --env-file .env pytest manual/<case>/<case>.py -s` | none |
| `benches/` | (data) the jigs' definitions, `<name>.toml`, named by `TEST_BENCH_<PROFILE>` | — | — |

`bench/` and `manual/` are in `norecursedirs`; `manual/` entry points carry no `test_` prefix. A bare
`pytest` never flashes a board. `pytest bench` checks the probe against its bench file first and refuses
the run (with the `bench/prepare.py` command to fix it) when they differ.

The bench cases: `basic/` (the fourteen self-checking sketches, and `pd_sink`, which negotiates with a real USB PD charger on a bench whose file says `facts.pd_source`; `TEST_PD_METER` adds a rough VBUS reading from an XY-FZ25), `trace/` (waveforms recorded and checked
through WireSkein: periph_probe, gpio_probe, adc_probe, reset_probe, i2c_probe, uart_probe),
`startup/crt0_probe` (the probe fills RAM and resets) and `regs/reg_probe` (registers read through the
probe against device-data; needs oep-probe-arduino 0.0.7 or later). Tests ask the bench file by
pad name and skip, with the reason, when a pad, a capture or a target is not wired.
