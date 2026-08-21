# ArduinoCore-CH32

[Japanese](README.ja.md)

A community-maintained Arduino core for WCH CH32 microcontrollers.

> [!IMPORTANT]
> This project is in the research and design phase. There is no installable or stable Arduino core release yet.

This is not an official WCH project. The name `CH32` identifies the target device family and does not imply endorsement by or affiliation with WCH.

## Goals

- Remain buildable with maintained toolchains and current Arduino tooling
- Describe support at the exact device, package, board, and validation level
- Isolate Arduino APIs from changes in WCH EVT and other low-level environments
- Automate reproducible builds, flashing, hardware-in-the-loop tests, and Board Manager releases
- Prefer low long-term maintenance cost over the shortest initial implementation

## Current direction

The following items are working proposals, not finalized specifications:

- Keep the default Arduino core small and separate optional EVT compatibility
- Use a pinned ArduinoCore-API revision as the Arduino-facing API boundary
- Own startup, CRT, vector-table, and linker behavior in this project
- Import only explicitly selected vendor files with locked provenance and hashes
- Generate board metadata and CI matrices from declarative device manifests
- Separate a stable upload frontend from replaceable programmer backends
- Combine host, compile, hardware-in-the-loop, logic-analyzer, and replay tests

The initial implementation is expected to focus on RISC-V CH32 devices. The long-term scope, including Arm-based CH32 devices and wireless SoCs, has not been decided.

## Build menus (**provisional**)

> The `printf` menu and the newlib-nano default are **proposed and not
> approved** ([approval status A-1](docs/approval-status.ja.md)). They may change.

| Menu | Default | What it selects |
|---|---|---|
| Part Number (`pnum`) | `ANY` | The part. `ANY` declares the smallest flash/RAM in the series, so a binary built for it fits every part |
| printf() float support (`printf`) | `none` | Whether `printf("%f")` works |

**With the default, `printf("%f")` prints nothing.** The runtime is newlib-nano,
which leaves out floating point conversion (proposed in
[ADR-0004](docs/adr/0004-runtime-and-cxx.ja.md), still `Proposed`). Anyone arriving from another Arduino core will hit this, so it
is stated plainly here.

Set the menu to `%f supported` when you need it. That costs about 19 KB of flash
(measured on CH32X035: 7.1 KB to 25.9 KB) and does not fit CH32V003's 16 KB.

`Serial.print(1.5, 2)` is the core's own implementation and always works,
independent of this menu.

## Repository layout

The repository root **is** the Arduino platform directory. For development,
symlink this root to `<sketchbook>/hardware/ch32-riscv-ug/ch32v`.

```text
platform.txt          build recipes
boards.txt            generated (tools/generate) - do not hand-edit
cores/arduino/        the core
  api/                unmodified ArduinoCore-API 1.5.2 snapshot (LGPL-2.1-or-later)
variants/<SERIES>/    pin definitions and linker scripts (both generated)
tools/                generate (boards/ld), index (Board Manager), vendor (import checks)
tests/                compile matrix, startup equivalence, sizebench
docs/                 design documents, ADRs, experiment records
vendor/               upstream pins: the ArduinoCore-API and TinyUSB snapshots,
                      and ch32-device-data.lock.toml - the one place recording
                      which tables boards.txt and variants/ came from
```

A release archive contains only `platform.txt`, `boards.txt`, `cores`,
`variants` and `libraries` (`PLATFORM_ENTRIES` in `tools/index/gen_index.py`).

## Development documentation

The initial research and design documents are currently maintained in Japanese:

- [Project handoff](docs/handoff.ja.md)
- [Documentation index](docs/README.ja.md)
- [Project scope](docs/project-scope.ja.md)
- [Architecture proposal](docs/architecture.ja.md)
- [Open questions](docs/open-questions.ja.md)

Stable user-facing documents will gain English `.md` versions as the project matures. Japanese documents use the `.ja.md` suffix.

## License

Code and documentation authored by this project are licensed under the [MIT License](LICENSE).

Third-party material retains its own license; the repository's MIT License does not relicense it.

| path | origin | License |
|---|---|---|
| `cores/arduino/api/` | unmodified copy of [arduino/ArduinoCore-API](https://github.com/arduino/ArduinoCore-API) tag `1.5.2` | **LGPL-2.1-or-later** ([bundled LICENSE](cores/arduino/api/LICENSE)) |

The pinned commit and a SHA-256 for every file are recorded in
[`vendor/arduino-core-api.lock.toml`](vendor/arduino-core-api.lock.toml); the CI
`api-sync` job verifies byte-for-byte equality with upstream on every PR
(rationale: [ADR-0009](docs/adr/0009-arduinocore-api-import.ja.md), Japanese).

Redistribution terms for WCH EVT and other vendor material are still
unresolved; no such files are currently imported.
