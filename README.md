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
| CPU Clock (`clock`) | per series | The system clock. Each entry names the frequency and what it allows (USB, Ethernet). Defaults: V20x / V30x 96 MHz, V103 72 MHz, X035 48 MHz, V00x 24 MHz, L103 / M103 8 MHz, M030 72 MHz, V205 160 MHz (M030 and V205 raised to their EVT defaults after a hardware check; the L103 / M103 PLL entries say "not checked", and an OEP probe (not a WCH-LinkE) cannot attach to an L103 running at 72 / 96 MHz - build test images with the boot hold in docs/upload-routes.ja.md). Ethernet on V208 / V203RB needs `60MHz (Ethernet, no USB)`. V407 / V467 start at 200 MHz (120 / 60 / 20 MHz selectable, checked on a V407WEU6). X315 starts at 20 MHz, 240 MHz selectable (HSI through the USBHS PLL, checked on an X315MCU6). Not yet on H41x. `getCpuFrequencyMhz()` / `getApbFrequency()` (as in arduino-esp32) return the clock; nothing changes it at run time |
| Clock Source (`clocksrc`) | `hsi` (V208: `hse`) | `hse` runs the same CPU Clock frequency from the external crystal (HSE). The crystal is the one EVT assumes (V00x 24 MHz; V103 / V20x / V205 / V30x / L103 8 MHz), which every bench board carries; V208's is 32 MHz, and on V208 `hse` comes first, since 10BASE-T asks for the crystal's accuracy (HSI linked on the bench too, out of the spec) (the 8 MHz entry has no PLL and stays on HSI). A crystal that does not start at boot (they start in 1-2.5 ms; the core waits about 10 ms) leaves the part on HSI at the same frequency instead of stopping; `CH32RV.clockSource()` says which. The HSE frequency USB HS and Ethernet assume follows this menu too. Not yet on M030, X315, V407 and H41x |
| Optimize (`opt`) | `oslto` | `-Os` with LTO: 9-24% less flash at the same speed (measured on CH32X035). `osltosr` adds `-msave-restore` for another 4-5%, but call-heavy code (elliptic-curve crypto) runs 7-9% slower. `osstd` builds without LTO |
| Floating Point (`rtlib`) | `fltf` | Double or float arithmetic, printf's `%f`, and newlib's float printf / scanf |
| parseFloat (`parsefloat`) | `auto` | How `Serial.parseFloat()` adds its digits up. `auto` follows Floating Point: integers under Float, double under Double |
| USART1-10, I2C1-2, SPI1-3 (`usartN`, `i2cN`, `spiN`) | `auto` | How that peripheral's pins are chosen. AUTO decides in `begin()` (the default pins, or those `setPins()` chose). FIX fixes them in the build (for pins other than the default it is smaller than `setPins()`: 472-1028 B for Serial, 28-100 B for Wire). Disable links nothing of the peripheral (dropping X035's other USARTs saves 456 B). `Serial`, `Serial1` ... name the enabled peripherals in number order, so a Disable moves the later names down. Software (I2C, SPI) makes that name (`Wire`, `SPI` ...) a SoftWire / SoftSPI on the peripheral's default pins, linking nothing of the peripheral (a V003 Wire sketch: 2996 B, against 4008 B on the hardware I2C) |
| Flash / RAM Split (`ramsplit`) | `256k_64k` / `512k_200k` (factory setting) | How the SRAM is split between the zero-wait flash copy and RAM: V303 / V305 / V307 / V317 (320 KB, five splits) and V407 / V467 RET6, VET6, WEU6 (512 KB + 200 KB or 576 KB + 136 KB) ([support-status.ja.md](docs/support-status.ja.md), Japanese). A non-factory entry has the core rewrite the option byte at boot |
| VIO18 Supply (`vio18`) | `hw` | H41x's VIO18 voltage. `hw` keeps the board's XO strap; the others are set by the core at boot |
| Reset Pin (`rstpin`) | `keep` (the chip as it is set) | On the series whose reset input is an option byte setting (the reset pin from the factory on V002-V006, a GPIO on V007, M007, X033, X035, M030, X305 and X315). `keep` does nothing and leaves the chip as it is set. `reset` and `gpio`, the factory value's entry too, have the core compare the option bytes at boot and, when they differ, rewrite them once and reset (276 B more on V003); a chip changed by another sketch or by ch32rv comes back with a sketch built for that entry. UIAPduino has no such menu (its HID bootloader relies on the factory reset pin) |
| Watchdog (`iwdg`) | `keep` (the chip as it is set) | Whether the IWDG starts with `wdtEnable()` (`sw`, the factory value) or runs from power-on (`hw`). Both have the core compare and rewrite the option byte at boot (264 B more on V003, 80 B when Reset Pin is chosen too, which shares the rewrite); `keep` does nothing. V00x, M007, X03x, X3x5, V20x, V205, V30x, L103 / M103, V407 / V467 |
| Erase Flash (`erase`) | `keep` | What an upload erases. Keep Store keeps the flash tail a sketch reserved with `CH32RV_FLASH_STORE` (ch32rv saves and writes it back: about 0.8 s more for 16 KB on V003, 1 s for 96 KB on V307). All Flash (`chip`) erases the whole chip. H41x's DFU route refuses All Flash. UIAPduino has no such menu (it uploads through HID alone) |

The printf family (`Serial.printf`, `printf`, `sprintf`, `snprintf`), `Serial.print(float)` and `String(float)` use the core's formatter.
The default (`fltf`) computes in float as AVR does and prints printf's `%f`: a few KB smaller than double without an FPU, over ten KB on V30x, X3x5 and H41x.
Pick `dbl` / `dblf` for 15 digits (GPS coordinates and the like). A sketch that uses printf but no floats is about 0.9 KB smaller under `flt` (1.5 KB with an FPU), which prints `%f` as written. Without an FPU, printing a float takes no soft-float: the float steps are done on its bits.
A sketch that uses neither floats nor printf is the same size under every entry.
`strftime()`, `gmtime_r()`, `localtime_r()` and `tzset()` are the core's too: the same text as newlib's, 2-10 KB smaller.
The parseFloat menu has `Serial.parseFloat()` add its digits up in double (`double`: the nearest float), in integers (`int`: the nearest float to 11 digits, no double arithmetic) or in float (`float`: the smallest, sometimes off by the last bit).
The entries, their costs and how the output differs from C are in [printf and floating point](docs/printf-float.ja.md) (Japanese).

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
```

Board Manager archives use an allowlist. `PLATFORM_ENTRIES` in
`tools/index/gen_index.py` is the source of truth for their contents.

## Documentation

- [Documentation index](docs/README.ja.md) (Japanese)
- [Example sketch guide](docs/examples.md): choose examples by purpose, with wiring, API use and adaptation notes
- [How a sketch is put together](docs/sketch-guide.ja.md) (Japanese; interrupts, not waiting, callbacks - the next read after Blink)
- [Series and hardware support](docs/support-status.ja.md) (Japanese)
- [Peripheral support](docs/peripheral-support.ja.md) (Japanese)
- [USB Device, Host, PD and example guides](docs/usb.ja.md) (Japanese; each example also has an English README)
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
| `libraries/FS/src/fatfs/` | FatFs R0.16 with its author's patches 1 and 2 | FatFs license (1-clause BSD-style) |
| `libraries/Network/src/lwip-src/` | lwIP 2.2.1 | BSD-3-Clause |
| `libraries/NetworkClientSecure/src/mbedtls-src/` | Mbed TLS 4.1.1 | Apache-2.0 or GPL-2.0-or-later |
| USB serial chip sequences in `libraries/CH32RVUsbHost/src/CH32RVUsbHostCdc.cpp` | rewritten from TinyUSB's cdc_host.c (after the Linux drivers) | MIT |

Pinned revisions and file hashes are recorded in `vendor/*.lock.toml` and checked by the vendor tools.
