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

ArduinoCore-API, WCH EVT, toolchains, and any other third-party material retain their respective licenses and usage terms. The repository's MIT License does not relicense third-party material.
