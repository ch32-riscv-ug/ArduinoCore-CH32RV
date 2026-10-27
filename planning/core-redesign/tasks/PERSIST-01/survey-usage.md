# PERSIST-01 survey: how Arduino-ecosystem projects actually persist data, and how much

Questions from the user: (1) "Is EEPROM (Arduino `EEPROM.h`) actually used?" (2) "Don't real projects usually save more than that?"

Method (2026-10-28). Three kinds of evidence, of different strength:

1. **Code-search counts** (Sourcegraph public instance, `.ino` files only, forks and archived repos excluded, one count per file). GitHub code search needs a login and grep.app blocked scripted access (Vercel checkpoint), so Sourcegraph was the only reachable index. Its public index covers a subset of GitHub (mostly repos with some stars). Almost every query reported `shard-match-limit`, i.e. **the counts are lower bounds and not exhaustive**. Repeating two queries gave 2441 → 2361 (`#include <EEPROM.h>`) and 5465 → 4038 (`SD.begin(`), so the counts vary **±25 % between runs**. Use them only for order-of-magnitude comparisons.
2. **Arduino Library Manager index** (`library_index.json.gz`, 10 284 distinct library names, latest version of each). This measures what people *publish*, not what they use: weak evidence of use.
3. **Hand-read samples**: 25 sketches that call `EEPROM.put(` (the first Sourcegraph results, ranked by repo stars), 40 CH32V003 application repos (39 from the top GitHub repo-search results for "ch32v003", plus the game console found via Hackaday.io), and 5 V003 storage libraries, 30 `Preferences` results, 165 `SD.open(` lines from 37 repos. The samples are small and biased toward starred repos.

F = fact (read or counted). I = inference (labelled). W = weak evidence.

---

## 1. Summary table

| Storage method | Evidence of use (counts) | Typical data | Typical size | Write frequency seen |
|---|---|---|---|---|
| Built-in `EEPROM.h` (AVR real EEPROM; ESP8266/ESP32/RP2040/STM32 emulate it in flash) | F: `#include <EEPROM.h>` in ≥2.4k `.ino` files out of ≥62k `.ino` files with `void setup(` (≈4 %, lower bound, ±25 %). `EEPROM.read(` 1.9k, `.write(` 1.6k, `.put(` 0.67k, `.get(` 0.64k, `.update(` 0.36k files. `EEPROM.begin(` 1.0k and `.commit(` 0.9k files, the ESP-style emulation calls, so roughly 40 % of EEPROM users are on flash-emulating cores (I). ATtiny85: 202 of 1426 `.ino` files that mention "ATtiny85" also mention EEPROM (≈14 %) | Calibration factors and offsets, mode and menu settings, a magic/version byte, device address/ID, WiFi/MQTT credentials (on ESP), boot or file counters | Hand sample of 25 sketches: median ≈ 25 B; 19 of 25 store ≤ 64 B; 4 store ≈100–220 B (3 WiFi/MQTT credentials or tokens on ESP, 1 scale calibration + model data); none above 512 B | 22 of 25 write on user action / calibration / config command, or once at first boot; 1 writes a counter on every boot; 1 writes after 4 s of no change (debounce); 1 writes on an OTA event |
| ESP32 `Preferences` (NVS) | F: `Preferences.h` in ≈470 `.ino` files (≈1/5 of the EEPROM.h count). Library index: 11 libraries depend on `Preferences`; 11 NVS/Preferences wrapper libraries in "Data Storage" | Same kinds as EEPROM, as named keys: WiFi/host/IP settings, call signs, tokens, last channel, one counter example (M5Stack). One CNC controller (nanoels h4) stores whole G-code programs as strings | Tens to a few hundred bytes per project; the G-code case is KB-scale (I, not measured) | On settings change, mostly |
| LittleFS / SPIFFS (file system in flash, ESP8266/ESP32/RP2040) | F: `SPIFFS.begin(` ≈2.9k files, `LittleFS.begin(` ≈1.4k files: together more than `EEPROM.h` | Web pages and assets for the built-in web server, JSON config files, logs | KB to MB (I: these are file systems of 64 KB–several MB) | Assets: written at upload; config: on change; logs: periodic |
| SD card (`SD.h`, SdFat) | F: `SD.begin(` 4.0k–5.5k files (more than `EEPROM.h`), `#include <SdFat.h>` ≈0.9k. Library index: 37 libraries depend on `SD`, 12 on `SdFat`, 29 on the Adafruit SdFat fork; SdFat repo 1213 stars | Data logging (CSV, GNSS UBX streams), audio/image/video files, configuration files | KB to GB | Periodic/continuous logging dominates the sample: of 37 repos, 29 open files for writing; examples: GNSS log append, sensor CSV, WAV recording |
| SAMD/STM32 `FlashStorage` libraries (internal flash emulation outside the core) | F: `#include <FlashStorage…>` in only 27 files. Library index: 13 internal-flash-emulation libraries; FlashStorage_SAMD has 16 dependants, FlashStorage_STM32 13 (I: mostly the same author's WiFi-manager libraries). cmaglie/FlashStorage 213 stars | Settings struct, WiFi-manager credentials | A struct, typically < 256 B (I) | On change |
| External I2C/SPI EEPROM, FRAM (24Cxx, AT24, 25xx, MB85) | F: library includes are rare in `.ino` files: `I2C_eeprom.h` 31, `extEEPROM.h` 23, `SparkFun_External_EEPROM.h` 0 (query may have missed); raw `Wire.beginTransmission(0x50…)`-style access ≈54. But **52 of the 274 "Data Storage" libraries** (the largest single group) drive external EEPROM/FRAM/SRAM chips. Stars: I2C_EEPROM 121, extEEPROM 114, SparkFun 67 | Larger tables, logs, programs (e.g. Tiny BASIC "disks"), data that must survive firmware upload, high-endurance counters (FRAM) | 256 B – 128 KB chips | Mixed; FRAM is chosen when writes are frequent (I) |
| SPI NOR flash (W25Qxx, SerialFlash, SPIMemory) | F: includes of SPIMemory/SPIFlash/SerialFlash/Adafruit_SPIFlash ≈745 files (count not hand-checked). 15 libraries; SPIMemory 506 stars, SerialFlash 449 | Audio samples, logs, CircuitPython/FAT file systems | MB | Mostly bulk writes |
| CH32V003 option-byte "EEPROM" (openwch core) and last-page flash | See section 3: 7 of 40 sampled V003 application repos persist anything; 6 in the internal flash tail, 1 in an external 24Cxx; none uses option bytes | Settings, high scores, Modbus address/baud, a BASIC program, a capacity counter | 4–1024 B; 4 of 7 ≤ 64 B (one page) | Mostly on user action; 1 periodic (60 s) |

---

## 2. Arduino ecosystem (section A)

### 2.1 Code-search counts (F, but noisy lower bounds)

All on `.ino` files in the Sourcegraph public index (queries in the Sources list).

| Query | Files |
|---|---|
| `void setup(` (denominator) | ≥ 62 289 |
| `#include <Wire.h>` (reference) | ≥ 12 936 |
| `WiFi.begin(` (reference: share of network sketches) | ≥ 7 297 |
| `SD.begin(` | 4 038 – 5 465 (two runs) |
| `SPIFFS.begin(` | 2 867 |
| `#include <EEPROM.h>` | 2 361 – 2 441 (two runs); `EEPROM.h` anywhere 2 984 |
| `EEPROM.read(` | 1 928 |
| `EEPROM.write(` | 1 608 |
| `LittleFS.begin(` | 1 431 |
| `EEPROM.begin(` (ESP-style emulation) | 998 |
| `#include <SdFat.h>` | 944 |
| `EEPROM.commit(` | 919 |
| SPI-flash library includes (regexp) | 745 |
| `EEPROM.put(` | 666 |
| `EEPROM.get(` | 644 |
| `ESP32 AND EEPROM.begin` | 490 |
| `Preferences.h` | 470 (`#include <Preferences.h>`: 446) |
| `EEPROM.update(` | 358 |
| `Wire.beginTransmission(0x50-0x57 or EEPROM…)` | 54 |
| `#include "I2C_eeprom.h"` | 31 |
| `#include <FlashStorage…` | 27 |
| `#include <extEEPROM.h>` | 23 |
| `#include <SparkFun_External_EEPROM.h>` | 0 |

Facts read from the table:
- `EEPROM.h` is used, in roughly 1 sketch in 25 of this index (≥ 2.4k / ≥ 62k). This is about one fifth of the `Wire.h` count.
- File-system and SD use is **not smaller** than EEPROM use: `SD.begin` (4–5.5k) and `SPIFFS.begin` + `LittleFS.begin` (≈4.3k) each exceed `#include <EEPROM.h>` (≈2.4k).
- Byte API (`read/write`, 1.6–1.9k) is used more often than the struct API (`put/get`, ≈0.65k).
- On ESP boards, the old `EEPROM.begin` emulation (≈1.0k files) is used about twice as often as `Preferences.h` (≈0.47k).
- Including an external-EEPROM library in a sketch is rare (≤ 31 per library).

Inference:
- I: the file-system/SD numbers come mostly from ESP32/ESP8266/Teensy/RP2040 class boards (the SD sample in 2.4 is dominated by LilyGO, M5Stack, SparkFun RTK, Teensy Audio). They show what projects do **when the hardware has MBs**, not what a 16 KB part needs.
- I: external EEPROMs are probably used more often than the include counts show, because many sketches talk to a 24Cxx with raw `Wire` calls and variable addresses that a literal query does not match, and because the 52 published drivers suggest steady demand.

### 2.2 Library Manager index (F; weak evidence of use)

- 10 284 distinct libraries; 274 in category "Data Storage".
- Classification of those 274 by keyword (automatic, checked by eye; a few libraries are misfiled, ±3 per group):

| Group | Libraries |
|---|---|
| Not persistent storage (containers, ring buffers, PROGMEM helpers, web config UIs) | 79 |
| External EEPROM / FRAM / serial SRAM chip drivers | 52 |
| File system / database / key-value on a file system (LittleFS, SPIFFS, SQLite, JSON) | 47 |
| Wrappers around the built-in EEPROM API (typed access, CRC, wear levelling, ESP rotation) | 32 |
| SD card | 25 |
| SPI NOR flash | 15 |
| Internal-flash emulation (FlashStorage family, Due, nRF, RP2040) | 13 |
| ESP32 NVS / Preferences wrappers | 11 |

- Across all categories, names/descriptions mention: "EEPROM" 189 libraries, wear levelling 26 (≈12 of them EEPROM wear-levelling libraries: EEPROM_WL, EEWL, RingEEPROM, EEPROMWearLevel, LongLiveThEEPROM, EEManager, EEPROM_Rotate, …), data logging 20.
- Declared dependencies: `ArduinoJson` 284 (reference), `SD` 37, Adafruit SdFat fork 29, Adafruit SPIFlash 20, FlashStorage_SAMD 16, FlashStorage_STM32 13, SdFat 12, Preferences 11, `EEPROM` 8, I2C_EEPROM 4. I: `EEPROM` is bundled with each core, so libraries rarely declare it; this count is not comparable.
- GitHub stars (2026-10-28): greiman/SdFat 1213, Marzogh/SPIMemory 506, PaulStoffregen/SerialFlash 449, cmaglie/FlashStorage 213, lorol/LITTLEFS 201, adafruit/Adafruit_SPIFlash 197, thijse/Arduino-EEPROMEx 173, RobTillaart/I2C_EEPROM 121, JChristensen/extEEPROM 114, vshymanskyy/Preferences 109, jwrw/ESP_EEPROM 88, sparkfun External EEPROM 67, RobTillaart/FRAM_I2C 45, khoih-prog/FlashStorage_STM32 43.

### 2.3 Hand sample: 25 sketches that call `EEPROM.put` (F)

First Sourcegraph results for `EEPROM.put(` in `.ino` files, one file per repo, library example files skipped. Sizes are from the struct/variable declarations in the file; "≈" means estimated from types.

| # | Project (repo) | Board | What is stored | Bytes | When written |
|---|---|---|---|---|---|
| 1 | Makeblock-Libraries (MegaPi firmware) | Mega | balance-car PID gains, check bytes | ≈ 45 | on parameter command |
| 2 | grbl `grblWrite_BuildInfo` | UNO | build-info string + checksum | ≤ 81 | once (utility sketch) |
| 3 | nightflyer88/CG_scale | ESP8266/AVR | load-cell calibration, model name, distances, battery type | ≈ 100–200 | in the settings menu |
| 4 | AgOpenGPS Boards (≈18 firmware variants) | Nano/Teensy/ESP32 | magic id + IP address; settings struct | 6 (+ settings struct) | first boot; on config from PC |
| 5 | AR488 (GPIB adapter) | Nano/Mega | config struct (addresses, modes, 48-char version string) | ≈ 64 | on `++savecfg` command |
| 6 | fustyles ESP32-CAM video recorder | ESP32 | magic + file-group number | 8 | **every boot** (counter) |
| 7 | cotestatnt esp-fs-webserver remoteOTA | ESP32 | firmware version | 4–8 | on OTA update |
| 8 | ly2707/uart-wifi-transmissible | ESP32 | mode, 2 baud rates, client id, flags | ≈ 40 | on config command |
| 9 | LilyGO T-Embed PLAMEN (SI5351 radio) | STM32 | frequency, menu values | ≈ 8 | 4 s after the last tuning change (debounced) |
| 10 | tzapu WiFiManager example | ESP8266 | settings struct {float, int, char[20], ip} | 32 | on portal save |
| 11 | dmcke5/DIY_VR | — | IMU calibration, LED colour, flag | ≈ 30–50 | after calibration / colour change |
| 12 | kbox (Adafruit BNO055 restore_offsets) | Teensy | sensor id + 22-byte calibration offsets | 26 | after calibration |
| 13 | DiyYari/Cronos-Control | — | speeds, dead zone, sensitivity, button names (char[15] each) | ≈ 80 | in the settings menu |
| 14 | MPU9250_WE autocalib example | ESP | accel + gyro offsets (2 × xyzFloat) | 24 | after calibration |
| 15 | Kris-Sekula/EPROM-EMU-NG | Nano | HW version string, ROM size, flags | ≈ 10 | on command; version once |
| 16 | WB2CBA/ADX (FT8 transceiver) | Nano | mode, band slot, calibration factor, flag | ≈ 12 | on button / calibration |
| 17 | ElektroJonas DIY quadcopter | ESP32 | IMU offsets + valid flag | ≈ 13–25 | after calibration |
| 18 | KALTCHUK Z80 Modular (RS-485 coil) | Nano | baud, Modbus id | 4 | on command |
| 19 | RobSmithDev ArduinoFloppyDiskReader | Nano | a few option bytes | ≈ 10 | on host command |
| 20 | olkal/CG_scale | ESP/AVR | 3 calibration floats + 3 flags | 15 | after calibration |
| 21 | kp-bit/xblinds | ESP8266 | id, WiFi SSID/password, AP flag, MQTT server/port/user/pass | ≈ 220 (512 reserved) | on config portal save |
| 22 | remrc Self-Balancing-Bike | ESP32 | IMU offsets | ≈ 12 | after calibration |
| 23 | DesconBelgrade/Klimerko (air-quality node) | ESP8266 | device id[32], token[64], flags, temp offset | ≈ 100 (256 reserved) | on serial config |
| 24 | ericgu CaptivePortalAdvanced copy | ESP8266 | SSID, password, ok flag | ≈ 100 (512 reserved) | on portal save |
| 25 | jbaumann/attiny_daemon (UPS controller) | ATtiny | ≈ 10 voltage thresholds/coefficients, timeouts | ≈ 30 | on I2C command from host |

Facts from the sample:
- Sizes: 19 of 25 store ≤ 64 B; 21 of 25 ≤ 128 B; the four at ≈100–220 B are 3 credential/token sketches on ESP boards and CG_scale (calibration, model name, distances). Median ≈ 25 B (depends on the estimates). Several ESP sketches **reserve** 256–512 B with `EEPROM.begin(n)` while using less.
- Data kinds: calibration (9), settings/mode/menu values (10), credentials or tokens (3), identity/version/magic (several, usually alongside other data), counter (1).
- Write triggers: user action, calibration, a host/serial/I2C command, or first boot in 22 of 25; one boot counter (#6); one debounced write after a change (#9); one OTA event (#7). **No sample sketch writes EEPROM periodically from a timer.**

### 2.4 Samples of Preferences and SD use (F, small)

- `Preferences` (30 results, 10 repos): last WiFi channel (Heimdall WiFi radar), last NMEA2000 node address, inverter gateway network config (hostname, static IP, netmask, gateway), LoRa APRS call signs and fixed position, Telegram bot token and chat id, a boot counter (M5Stack examples), and in kachurovskiy/nanoels h4 whole **G-code programs** stored as strings. Writes are on settings changes, except the counter.
- `SD.open(` (165 lines, 37 repos): 29 of 37 repos open files for writing. Examples: SparkFun RTK GNSS firmware appending UBX logs, sensor CSV/data logs (HiGrow, SOILSENS, MAX30102), WAV/RAW recording (Teensy Audio, VS1053, T-Embed CC1101), JPEG/MJPEG playback, configuration text files (EvilCrowRF `wifi_config.txt`, Audioplayer). The boards are mostly ESP32/Teensy-class.

### 2.5 Answer material for the user's questions (I)

- (1) EEPROM is used: about 4 % of sketches in this index include `EEPROM.h` (lower bound), and on ATtiny85 sketches the share is ≈14 %. It is one of the commoner persistence methods, but **not** the most common: SD and flash file systems each appear in more sketches.
- (2) "More than that" is true **for projects on boards with MBs of storage** (ESP32 web assets, SD logging, audio). Where projects use the EEPROM API, the sample says they save small things: tens of bytes, ≤ 64 B in 19 of 25, ≤ 256 B for WiFi credentials. The data that is "more" (logs, media, files) goes to SD, SPI flash or a file system, not to `EEPROM.h`.
- Unknown: whether the projects that use SD/file systems would have used a few hundred bytes of internal storage if it existed; how much of the SD count is examples/tutorial copies.

---

## 3. Small MCUs (section B)

### 3.1 CH32V003: 40 application repos sampled (F)

39 of the top GitHub repo-search results for "ch32v003" (871 repos in total), skipping tools, SDKs and pure libraries, plus max7d3/CH32V003-MiniGameConsole (found via Hackaday.io). The 5 storage libraries found for "ch32v003 eeprom" / "flash storage" were read separately. Each repo was grepped for flash erase/program, option-byte writes, and external-EEPROM access, and the hits were read.

Applications that persist anything (7 of 40):

| Project | Where | What | Bytes | When written |
|---|---|---|---|---|
| bitbank2/Pocket_CO2 | page at 0x08003C00 (64 B page) | 4 ints: mode, alert, frequency, period | 16 | when leaving the menu, only if changed; not in calibration mode |
| ma6254/ch32v003_modbus | page at 0x08003C00 | magic, firmware size/CRC, Modbus address, baud, mode | 16 | on a Modbus config write |
| ohdarling/CH32V003-USBMeter | last page 0x08003FC0, 64-B RAM copy | accumulated energy (mWh) | 4 | **every 60 s while it increases** (periodic counter) |
| max7d3/CH32V003-MiniGameConsole (Hackaday.io 205032) | last page 0x08003FC0 | magic + high scores | 64 (16 words) | on a new high score |
| shippoiincho/TinyBASIC | last 512 B (8 pages, 0x08003E00) | BASIC program | 512 | on `SAVE` command |
| shippoiincho/tk80_ch32v003 (TK-80 emulator) | last 1 KB (16 pages, 0x08003C00) | emulated RAM image | 1024 | on the emulated "STORE DATA" key |
| Stan6314/TinyBasRV | **external 24Cxxx I2C EEPROM** ("disk", address selectable) | Tiny BASIC programs; the repo ships an EEPROM image with 8 example programs | chip-sized (KBs) | on save command |
| (option bytes via openwch EEPROM library) | — | none of the 40 sampled apps used it | — | — |

The other 33 sampled applications (USB HID devices, FM radio, games without saving, LED pendants, oscilloscopes, NFC, speech recognition, Linux emulator, etc.) persist nothing. Many wagiminator projects unlock the flash only to jump to the bootloader (`BOOT_now()`), not to store data.

Libraries/stand-alone stores for V003 found by the search (no counts of use): MoonFox2006/CH32V003_EEPROM (7 stars), shakir-abdo/ch32v003-eeprom ("up to 10 variables", 16-bit, CRC; 6 stars), invariablyafk/CH32V-FlashStorage (15 stars; README: "user config settings", "calibration values", "for stuff that doesn't change much"), AndyTakker/SettingsStore (README: settings and configuration, "does not assume active rewriting", no wear levelling), shakir-abdo/CH32V003J4M6-AT24C32 (external AT24C32 driver).

Facts:
- 7 of 40 (≈18 %) sampled V003 applications persist data. 4 of the 7 use ≤ 64 B (4, 16, 16, 64 B); 2 use 512 B / 1 KB of internal flash (both save a user program or memory image, i.e. a "file"); 1 uses an external EEPROM.
- The external-EEPROM project it stores user programs (KB-scale). It is the only sampled case where internal space is plausibly too small (I: the 16 KB flash also holds the interpreter).
- 1 of 7 writes periodically (USBMeter, 4 B every 60 s). I: at 10k-cycle page endurance this is ≈ 7 days of continuous operation; the project does no wear levelling.

### 3.2 openwch arduino_core_ch32 and ch32fun issues (F)

- openwch #63 (2024-02): "Any predictions for an example implementation for using flash? something similar to what we have in ESP which is an emulated EEPROM." A second user: "I just needed to save a byte and read it after restarting ch32". maxint-rd then wrote the EEPROM library (26 bytes in the option-byte area, PR #103).
- openwch #213 (2025-07): a CH32V103 user ("I am currently using ch32v103 in my product") found the library works only for the first 2 bytes on V103 and wrote his own 128-byte last-page emulation. A V203 user (2025-12) needed it too; PR #247 (2026-04) adds V20x support. The library's own comment lists usable sizes: 26 B (V003), 58 B (V103/V20x/V30x), 122 B (V002/V006/X033/X035).
- ch32fun #398 (2024-09): user wants "more than 2 bytes in optiondata", then: "I don't really need more than 2 bytes for my project". The optiondata example author: the code is written as 32-bit operations "to save code space … Considering the rather limited amount of flash in the 003 I felt it was more important to save space."
- ch32fun #945 (2026-08): a user wants the 1920-B BOOT area writable from firmware, partly so that "the boot area could also potentially be used as a separate non-volatile storage area"; counts bytes saved (228 B).
- ch32fun examples: of ≈60 V003 examples, 3 deal with persistence (`optiondata`: 2 user bytes; `optionbytes`; `flashtest`), none provides a store. V20x/V30x add `extendedflash`, `flash_write`, `bkp_test` (backup registers).
- Searches: "ch32v003 eeprom" 5 repos; issue searches "eeprom" 17 (openwch) and 4 (ch32fun).

### 3.3 Other small MCUs (F, thin)

- ATtiny85: 202 of 1426 `.ino` files that mention "ATtiny85" also contain "EEPROM" (≈14 %). Not hand-checked: a mention is not always a use (W).
- PY32, STM32C0, STM32F030: repo searches gave 53 (py32f002), 35 (stm32c011) repos, and only 1 ("py32 eeprom"), 0 ("stm32c0 eeprom") and 2 ("stm32f030 eeprom": an EEPROM-emulation demo and an external I2C EEPROM library) storage-related repos. Too few to say anything about what those projects persist.
- The cost/implementation side for these cores is in `survey-small.md`; not repeated here.

---

## 4. Write frequency (section C)

Facts, from the samples above:
- EEPROM.put sample (25): 22 on user action / calibration / command / first boot; 1 every boot; 1 debounced 4 s after a change; 1 on OTA. 0 timer-driven.
- CH32V003 sample (7): 6 on user action or event; 1 every 60 s (counter).
- Preferences sample: on settings change, plus a boot counter in vendor examples.
- SD sample: logging/recording is the main write pattern (29 of 37 repos open files for writing; continuous/periodic appends in GNSS and sensor loggers).
- Library index: 26 libraries mention wear; ≈12 are EEPROM wear-levelling libraries, i.e. some users write the EEPROM often enough to need it (W: existence, not frequency).

Inference:
- I: frequent writes exist (counters, energy totals, "last state") but are a minority in these samples; logs go to SD/flash file systems.
- Not found: any measurement of write rates across many projects. No survey or poll on EEPROM use was found (forum searches returned only individual threads, e.g. "touch calibration data takes about 32 bytes", W).

---

## 5. What remains unknown

- Absolute usage: Sourcegraph counts are lower bounds with ±25 % run-to-run noise and cover a starred subset of GitHub; the GitHub code-search totals (logged-in) and grep.app were not reachable.
- Whether the 25-sketch and 40-repo samples are representative (both are ranked by stars).
- How many sketches that include `EEPROM.h` are copies of the core examples or tutorials.
- External-EEPROM use through raw `Wire` calls (not countable with literal queries).
- Usage on PY32/STM32C0/F030 projects.
- The size of data in the SD/file-system group was not measured (inferred KB–MB from the media).

---

## 6. Sources

Code search (Sourcegraph stream API, queries as listed in 2.1, all with `file:\.ino$ select:file`):
- https://sourcegraph.com/search (API endpoint `https://sourcegraph.com/.api/search/stream`)

Library index:
- https://downloads.arduino.cc/libraries/library_index.json.gz (downloaded 2026-10-28)

Sketches in the EEPROM.put sample (paths inside each repo as indexed by Sourcegraph):
- https://github.com/Makeblock-official/Makeblock-Libraries
- https://github.com/FourThievesVinegar/solderless-microlab (grbl `grblWrite_BuildInfo`)
- https://github.com/nightflyer88/CG_scale, https://github.com/olkal/CG_scale
- https://github.com/AgOpenGPS-Official/Boards
- https://github.com/Twilight-Logic/AR488
- https://github.com/fustyles/Arduino (ESP32-CAM video recorder)
- https://github.com/cotestatnt/esp-fs-webserver
- https://github.com/ly2707/uart-wifi-transmissible
- https://github.com/Xinyuan-LilyGO/T-Embed (PLAMEN_SI5351_SI4732_STM32)
- https://github.com/tzapu/WiFiManager (examples/ParamsChildClass)
- https://github.com/dmcke5/DIY_VR
- https://github.com/sarfata/kbox-firmware (Adafruit_BNO055 restore_offsets)
- https://github.com/DiyYari/Cronos-Control
- https://github.com/wollewald/MPU9250_WE
- https://github.com/Kris-Sekula/EPROM-EMU-NG
- https://github.com/WB2CBA/ADX
- https://github.com/ElektroJonas/DIY-Quadcopter
- https://github.com/KALTCHUK/Z80_Modular_Computer
- https://github.com/RobSmithDev/ArduinoFloppyDiskReader
- https://github.com/kp-bit/xblinds
- https://github.com/remrc/Self-Balancing-Bike
- https://github.com/DesconBelgrade/Klimerko
- https://github.com/ericgu/Fade (CaptivePortalAdvanced credentials.ino)
- https://github.com/jbaumann/attiny_daemon

Preferences / SD samples (selection):
- https://github.com/kachurovskiy/nanoels, https://github.com/OpenInverterGateway/OpenInverterGateway, https://github.com/oe3cjb/TTGO-T-Beam-LoRa-APRS, https://github.com/G4lile0/Heimdall-WiFi-Radar, https://github.com/AK-Homberger/NMEA2000-Workshop, https://github.com/m5stack/M5StickC
- https://github.com/sparkfun/SparkFun_RTK_Firmware, https://github.com/PaulStoffregen/Audio, https://github.com/Xinyuan-LilyGO/LilyGo-HiGrow, https://github.com/joelsernamoreno/EvilCrowRF-V2

CH32V003 projects:
- https://github.com/bitbank2/Pocket_CO2
- https://github.com/ma6254/ch32v003_modbus
- https://github.com/ohdarling/CH32V003-USBMeter
- https://github.com/max7d3/CH32V003-MiniGameConsole, https://hackaday.io/project/205032-ch32v003-mini-game-console
- https://github.com/shippoiincho/TinyBASIC, https://github.com/shippoiincho/tk80_ch32v003
- https://github.com/Stan6314/TinyBasRV
- https://github.com/MoonFox2006/CH32V003_EEPROM, https://github.com/shakir-abdo/ch32v003-eeprom, https://github.com/invariablyafk/CH32V-FlashStorage, https://github.com/AndyTakker/SettingsStore, https://github.com/shakir-abdo/CH32V003J4M6-AT24C32
- Other sampled repos with no persistence: wagiminator/CH32V003-* (GameConsole, FM-Transmitter, FM-Receiver, USB-Joystick, USB-IR-Receiver, I2C-Knob, IR-Remote, USB-Knob, Mouse-Wiggler, MacroPad-plus, NeoPixel-Pendant), cpldcpu/BitNetMCU, CandleSense, GlowPoly, CH32HomeComputer, brian-smith-github/ch32v003_stt, cnlohr/ch32v003_3digit_lcd_usb, cnlohr/rv003usb, atomic14/ch32v003-audio, eeucalyptus/ch32v003-usbpd, ch32v003-temperature, cjdell/ch32-game-rust, MartinEssink/CH32V003-NFC, CH32V003-Linear-Encoder, tvlad1234/osillyscope, linux-ch32v003, limingjie/CH32V003-Servo, MoonFox2006/CH32V003_MicroOscilloscope, GRAVITYDIV10/ch32v003-forth, suapapa/rusty-bc250-atx, shippoiincho/beepmidi, 012345kxy/ch32v003-nrf24-power-switch, prosper00/ch32v003-charlieplex

Issues:
- https://github.com/openwch/arduino_core_ch32/issues/63
- https://github.com/openwch/arduino_core_ch32/issues/213
- https://github.com/openwch/arduino_core_ch32/pull/247
- https://github.com/cnlohr/ch32fun/issues/398
- https://github.com/cnlohr/ch32fun/issues/945
- https://github.com/cnlohr/ch32fun/tree/master/examples (optiondata, optionbytes, flashtest)

Library repos (stars): https://github.com/greiman/SdFat, https://github.com/Marzogh/SPIMemory, https://github.com/PaulStoffregen/SerialFlash, https://github.com/cmaglie/FlashStorage, https://github.com/lorol/LITTLEFS, https://github.com/adafruit/Adafruit_SPIFlash, https://github.com/thijse/Arduino-EEPROMEx, https://github.com/RobTillaart/I2C_EEPROM, https://github.com/JChristensen/extEEPROM, https://github.com/vshymanskyy/Preferences, https://github.com/jwrw/ESP_EEPROM, https://github.com/sparkfun/SparkFun_External_EEPROM_Arduino_Library, https://github.com/RobTillaart/FRAM_I2C, https://github.com/khoih-prog/FlashStorage_STM32

Weak (forum): https://forum.arduino.cc/t/calibration-in-eeprom/577891, https://forum.arduino.cc/t/library-usage-of-eeprom/208352
