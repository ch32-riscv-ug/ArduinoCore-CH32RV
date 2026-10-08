# Test plan

[Japanese plan](TEST_PLAN.ja.md) is the reference.
[Coverage](../docs/test-coverage.ja.md) inventories implementation and test definitions, not run results.

The redesign targets native Linux. Preserving the existing tests, harness, wiring or directory layout is not a requirement.
The new tests/ workspace runs board-free checks only. Tests, CI and maintenance tools do not depend on the archived suite.
Hardware directories contain documentation scaffolds, not sketches or executable fixtures; the hardware runner and local configuration resolver are not implemented.

Execution directories are split by equipment: board-free unit/host checks, builds, a single DUT, loopback, peers, instrumentation and manual operation.
Contracts distinguish core behavior, series/part/board differences, Arduino ecosystem integration, USB data, USB PD and bootloader HID.

Shared templates describe logical signals, required capabilities and safety. Physical wiring, identities, ports and calibration belong in ignored local configuration.
Use isolated Arduino CLI directories, the working-tree DUT core, pinned external dependencies and standard plugin DUT/peer lifecycle.

Separate missing implementation, missing test definitions, limited self-checks, external observations and actual execution results.
A required contract cannot pass a gate because missing equipment caused a skip.
Keep minimal Arduino upload/reset/monitor E2E here; probe internals belong to their owning repositories.

USB covers both DUT host / peer device and DUT device / peer host as equal, separate scenarios.
Do not assign a fixed USB role to CH32X035 or ESP32S3; select role-specific profiles and verify capabilities and power for each case.
Keep separate coverage and results for each role, with control logs independent of the USB link under test.
Enable USB only after both sides are prepared. A cheaper CH32X035 peer does not replace independent interoperability evidence.
TinyUSB vendoring is not Arduino USB integration.

The [README](README.ja.md) describes the new board-free workspace.
