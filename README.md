# ArduinoCore-CH32RV

[日本語](README.ja.md)

A community-maintained Arduino core for WCH CH32 RISC-V microcontrollers.
This is not an official WCH project.

## Installation

Add this URL under Arduino IDE → Preferences → Additional Boards Manager URLs,
then install **CH32 RISC-V** from Boards Manager:

```text
https://ch32-riscv-ug.github.io/ArduinoCore-CH32RV/package_ch32-riscv-ug_index.json
```

With arduino-cli:

```sh
arduino-cli core install ch32-riscv-ug:ch32rv \
  --additional-urls https://ch32-riscv-ug.github.io/ArduinoCore-CH32RV/package_ch32-riscv-ug_index.json
```

This also installs the compiler toolchain and the `ch32rv` upload/monitor tool.
In the IDE, select a WCH-Link, OEP probe, or supported board bootloader as the port,
then press Upload.

The [implementation matrix](docs/support-status.ja.md) lists which series have
build and upload support (Japanese).

## Design constraints

- The scope is RISC-V CH32 devices; Arm-based CH32F and wireless SoCs are excluded.
- The Arduino API boundary is a pinned ArduinoCore-API 1.5.2 snapshot.
- This project owns startup, CRT, vector tables, linker scripts, and peripheral implementations.
- Device, memory, pin, and interrupt data are generated from a pinned `ch32-device-data` revision.
- Normal builds are offline and consume committed generated files.
- The core is bare metal and does not bundle an RTOS.
- Upload and monitor operations use the bundled `ch32rv` tool.

The [ADRs](docs/adr/README.ja.md) record the rationale that is still needed to maintain these decisions.

## Build menus

| Menu | Default | Purpose |
|---|---|---|
| Part Number (`pnum`) | `ANY` | Exact part; `ANY` declares the smallest flash/RAM in the series so the result fits every part in that series |
| C Runtime Library (`rtlib`) | `nano` | Enables floating-point conversion in `printf("%f")` and/or `scanf("%f")` |

The default newlib-nano configuration omits floating-point conversion for `printf` and `scanf`.
Select `nanofp`, `nanofs`, or `nanofps` only when needed. These options add substantial
runtime code and may not fit small-flash parts; use the build size report as the authority.
`Serial.print(1.5, 2)` is independent of this menu.

## Repository layout

The repository root is the Arduino platform directory.

```text
platform.txt          build, upload, monitor, and debug recipes
boards.txt            generated; do not edit
cores/arduino/        core and pinned ArduinoCore-API snapshot
variants/             generated pin definitions and linker scripts
libraries/            bundled libraries and examples
tools/generate/       generation from ch32-device-data
tools/index/          Board Manager release generation
tools/vendor/         verification of pinned third-party sources
vendor/               upstream revision, hash, and license lock files
docs/                 current specifications, user guides, and ADRs
tests/                board-free checks and new test scaffolds
tests-legacy/         legacy tests and bench configuration
```

Board Manager archives use an allowlist. `PLATFORM_ENTRIES` in
`tools/index/gen_index.py` is the source of truth for their contents.

## Documentation

- [Documentation index](docs/README.ja.md) (Japanese)
- [Series and hardware support](docs/support-status.ja.md) (Japanese)
- [Peripheral support](docs/peripheral-support.ja.md) (Japanese)
- [Debug output](docs/debug-output.ja.md) (Japanese)
- [Debugger setup](docs/debugger.ja.md) (Japanese)
- [Flash-size analysis](docs/flash-size.ja.md) (Japanese)
- [Architecture decisions](docs/adr/README.ja.md) (Japanese)

## License

Code and documentation authored by this project are licensed under the [MIT License](LICENSE).
Third-party material retains its own license.

| path | origin | license |
|---|---|---|
| `cores/arduino/api/` | ArduinoCore-API 1.5.2 | LGPL-2.1-or-later |
| `libraries/TinyUSB/src/` | pinned TinyUSB 0.21.0 snapshot | MIT |

Pinned revisions and file hashes are recorded in `vendor/*.lock.toml` and checked by the vendor tools.
