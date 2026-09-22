# oep_smoke — OEP 開発用 probe で sketch を一巡する

`smoke.py` と同じ判定（`<name> READY` → `PING` → `test_<name>.py` の expectations 再生 → `FAIL` 無し・`failures=0`）を、
WCH-LinkE / probe-rs ではなく **OEP v0 probe**（ESP32-P4 + CH32X035F8U6 fixture）で行う。

- 書込み: `oep_client.v0.flash_image.program_image`（ESIG preflight、physical page 差分、probe 内 CRC32 verify、reset）
- console: probe の `fixture.uart` を lease（P4 RX=12 ← DUT PB0/USART4 TX、P4 TX=6 → DUT PB1）。sketch は `CH32_SERIAL_DEFAULT=4` で build
- 判定: `smoke.py` の `expectations()` を流用。文字列 expectation と `done failures=` 行

```sh
uv run tests/manual/oep_smoke/oep_smoke.py --sketch core_api
uv run tests/manual/oep_smoke/oep_smoke.py --sketch all --result-json /tmp/oep-smoke.json
```

probe firmware（oep-probe-arduino `examples/Esp32P4X035Probe`）は別途転送しておく。client は隣の checkout
`../../dev_oep/oep-client-python/src` を既定で参照する（`--oep-client` で変更）。port は `OEP_PROBE_PORT` か `--port`。

2026-09-22: basic 14 sketch が F8U6 で 14/14 PASS（1 sketch ≈ 30 s、書込み 0.6〜0.8 s）。
