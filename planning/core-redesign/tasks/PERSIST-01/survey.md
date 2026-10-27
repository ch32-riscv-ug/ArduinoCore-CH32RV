# Persistent settings on flash-only MCUs: survey for ArduinoCore-CH32RV

Date: 2026-10-27. Scope: what other cores and frameworks do, what users store, and how upload tools treat the storage region.
Each section separates **Fact** (with a source) from **Inference** (mine, not stated by the source).
The draft (docs/persistence-spec.ja.md) was read for context only. This survey does not evaluate it.

Sources fetched during the survey (some code was downloaded and grepped, as noted):
- [ESP-IDF NVS] https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/storage/nvs_flash.html
- [Preferences] https://docs.espressif.com/projects/arduino-esp32/en/latest/tutorials/preferences.html
- [esp32 EEPROM.cpp] https://github.com/espressif/arduino-esp32/blob/master/libraries/EEPROM/src/EEPROM.cpp
- [esp32 boards.txt] https://github.com/espressif/arduino-esp32/blob/master/boards.txt
- [esp32 default.csv] https://github.com/espressif/arduino-esp32/blob/master/tools/partitions/default.csv
- [esp8266 EEPROM.cpp] https://github.com/esp8266/Arduino/blob/master/libraries/EEPROM/EEPROM.cpp
- [esp8266 fs doc] https://arduino-esp8266.readthedocs.io/en/latest/filesystem.html
- [esp8266 boards.txt] https://github.com/esp8266/Arduino/blob/master/boards.txt
- [esp8266 4m2m.ld] https://github.com/esp8266/Arduino/blob/master/tools/sdk/ld/eagle.flash.4m2m.ld
- [esptool] https://docs.espressif.com/projects/esptool/en/latest/esp32/esptool/basic-commands.html
- [pico EEPROM doc] https://arduino-pico.readthedocs.io/en/latest/eeprom.html
- [pico EEPROM.cpp] https://github.com/earlephilhower/arduino-pico/blob/master/libraries/EEPROM/src/EEPROM.cpp
- [pico fs doc] https://arduino-pico.readthedocs.io/en/latest/fs.html
- [pico boards.txt / platform.txt] https://github.com/earlephilhower/arduino-pico/blob/master/boards.txt , .../platform.txt
- [picotool README] https://github.com/raspberrypi/picotool/blob/master/README.md
- [stm32duino wiki] https://github.com/stm32duino/Arduino_Core_STM32/wiki/API#eeprom-emulation
- [stm32_eeprom.c] https://github.com/stm32duino/Arduino_Core_STM32/blob/main/libraries/EEPROM/src/utility/stm32_eeprom.c
- [stm32CubeProg.sh] https://github.com/stm32duino/Arduino_Tools/blob/main/stm32CubeProg.sh
- [stm32 platform.txt] https://github.com/stm32duino/Arduino_Core_STM32/blob/main/platform.txt
- [FlashStorage_STM32] https://github.com/khoih-prog/FlashStorage_STM32
- [FlashStorage SAMD] https://github.com/cmaglie/FlashStorage
- [nRF52 InternalFS] https://github.com/adafruit/Adafruit_nRF52_Arduino/blob/master/libraries/InternalFileSytem/src/InternalFileSystem.cpp
- [Meshtastic nRF52 PR] https://github.com/meshtastic/Adafruit_nRF52_Arduino/pull/1
- [TDBStore.h] https://github.com/ARMmbed/mbed-os/blob/master/storage/kvstore/tdbstore/include/tdbstore/TDBStore.h
- [mbed PR 11987] https://github.com/ARMmbed/mbed-os/pull/11987
- [mbed data architecture] https://os.mbed.com/docs/mbed-os/v6.5/apis/data-architecture.html
- [Portenta KVS tutorial] https://docs.arduino.cc/tutorials/portenta-h7/por-ard-kvs (and the source PR https://github.com/arduino/docs-content/pull/506)
- [tronche TDBStore] https://tronche.com/blog/?p=434
- [Zephyr NVS] https://docs.zephyrproject.org/latest/services/storage/nvs/nvs.html
- [Zephyr ZMS] https://docs.zephyrproject.org/latest/services/storage/zms/zms.html
- [Zephyr settings] https://docs.zephyrproject.org/latest/services/storage/settings/index.html
- [Zephyr flash_parameters] https://docs.zephyrproject.org/latest/doxygen/html/structflash__parameters.html
- [Zephyr nvs.c / zms.c v4.2.0] https://github.com/zephyrproject-rtos/zephyr/blob/v4.2.0/subsys/fs/nvs/nvs.c , .../subsys/fs/zms/zms.c
- [littlefs DESIGN] https://github.com/littlefs-project/littlefs/blob/master/DESIGN.md
- [QMK EEPROM] https://docs.qmk.fm/drivers/eeprom
- [ESPHome prefs] https://esphome.io/components/preferences/ ; [ESPHome esp32 preferences.cpp] https://github.com/esphome/esphome/blob/dev/esphome/components/esp32/preferences.cpp
- [Tasmota settings.ino] https://github.com/arendst/Tasmota/blob/development/tasmota/tasmota_support/settings.ino ; [Tasmota Commands] https://tasmota.github.io/docs/Commands/ ; [Tasmota Upgrading] https://tasmota.github.io/docs/Upgrading/
- [Marlin] https://marlinfw.org/docs/features/eeprom.html and the HAL sources found via search (git.popov.link mirror of Marlin/src/HAL/STM32F1/eeprom_flash.cpp, LPC1768/eeprom_flash.cpp)
- [openwch EEPROM] https://github.com/openwch/arduino_core_ch32/tree/main/libraries/EEPROM
- [ch32fun examples] https://github.com/cnlohr/ch32fun/tree/master/examples
- [RNT Preferences] https://randomnerdtutorials.com/esp32-save-data-permanently-preferences/
- [WiFiManager README] https://github.com/tzapu/WiFiManager ; [AutoConnectWithFSParameters] https://github.com/tzapu/WiFiManager/blob/master/examples/Parameters/SPIFFS/AutoConnectWithFSParameters/AutoConnectWithFSParameters.ino
- [AVR eeprom_write] https://github.com/arduino/ArduinoCore-avr/blob/master/libraries/EEPROM/examples/eeprom_write/eeprom_write.ino
- [BNO055 restore_offsets] https://github.com/adafruit/Adafruit_BNO055/blob/master/examples/restore_offsets/restore_offsets.ino ; [Adafruit_BNO055.h] same repo
- [grbl settings.h] https://github.com/gnea/grbl/blob/master/grbl/settings.h
- [PIO forum] https://community.platformio.org/t/esp32-how-to-disable-all-flash-erase-during-upload/40219

---

## 1. Platforms

The criteria are the same for every platform: API shape, storage format, minimum flash, RAM, power-loss guarantee, wear leveling, survival across re-upload, where the size is chosen, and pitfalls.

### 1.1 ESP32: NVS, Preferences, and EEPROM-on-NVS

**Fact**
- API (Preferences): `begin(namespace, readOnly)` / `end()`, typed `putX`/`getX(key, default)`, `putBytes`/`getBytes`, `isKey`, `remove`, `clear`, `freeEntries`. Namespace and key names have at most 15 characters. Only one namespace can be open at a time. The docs present it as "the replacement for the Arduino EEPROM library" and recommend it for "many small values". For large data they point to LittleFS. [Preferences]
- Format (NVS): a log-structured key-value store. Pages are 4096 B (a 32 B header, a 32 B entry-state bitmap and 126 entries of 32 B). An update appends a new entry and marks the old one erased. State changes "only clear bits (writing 0s)". Page states are Empty/Active/Full/Erasing/Corrupted, with one Active page, ordered by sequence number. Strings can be up to 4000 B, and blobs can span pages. [ESP-IDF NVS]
- Minimum flash: "default minimal size … 12 KiB (0x3000)", meaning 3 pages with one kept empty. A read-only partition can be 4 KiB. The Arduino default partition table has `nvs` at 0x9000 with size 0x5000 (20 KB). [ESP-IDF NVS] [esp32 default.csv]
- RAM: about 22 KB of heap per 1 MB of partition, about 5.5 KB per 1000 keys, and a page hash list of 128-640 B. [ESP-IDF NVS]
- Power loss: a power loss "should not result in loss of data, except for the new key-value pair" being written. Init "with any random data present in flash" is supported. Recovery at init may erase and rewrite pages. [ESP-IDF NVS]
- Wear: appending reduces erases by about "a factor of 126" for single-entry values. [ESP-IDF NVS]
- Pitfalls: fragmentation causes `NOT_ENOUGH_SPACE` even when stats show free entries. A truncated or changed partition gives `NO_FREE_PAGES` (fix: erase the partition). Interrupted erases under unstable power cause the same error (an option, `CONFIG_NVS_FLASH_VERIFY_ERASE`, retries). Init takes about 0.5 s per 1000 keys. Erased data remains readable until purged. [ESP-IDF NVS]
- EEPROM library on ESP32: stores one NVS blob, namespace and key `"eeprom"`. `begin(size)` resizes the blob, filling new bytes with 0xFF. A RAM mirror of `size` bytes exists for the object's lifetime. `commit()` returns early if not dirty and otherwise writes the whole blob. `write()` sets dirty only on a change. `getDataPtr()` and `writeBytes()` set it always. [esp32 EEPROM.cpp]
- Where the size is chosen: the partition table, picked from the IDE "Partition Scheme" menu. [esp32 boards.txt]
- Re-upload: the IDE menu "Erase All Flash Before Sketch Upload" (Disabled/Enabled, which adds `-e`). A default `write-flash` erases only the 4 KB sectors it writes. [esp32 boards.txt] [esptool] A forum answer says NVS survives a normal upload with the default table. [PIO forum]

**Inference**
- The NVS format depends on reprogramming the same bitmap word to clear bits. It cannot run on CH32 parts that read erased flash as 0xE339E339 and cannot program a word twice.
- The 12 KB minimum and the RAM cost rule it out on V003-class parts.
- The value of the design to users is the API: named keys, typed get with a default, and no explicit size.

### 1.2 ESP8266: EEPROM with begin/commit

**Fact**
- API: Arduino EEPROM plus `begin(size)` (up to the 4096 B sector, rounded up to 4), `commit()` and `end()`. Reads and writes go to a RAM buffer of `size` bytes. A dirty flag skips unchanged commits. [esp8266 EEPROM.cpp]
- Format: one sector. `commit()` erases the sector and then writes the buffer. There is no backup copy. [esp8266 EEPROM.cpp]
- Location: a linker symbol `_EEPROM_start`. In the 4 MB/2 MB-FS map, the FS ends at 0x405FA000 and EEPROM is at 0x405FB000 (4 KB), just below the SDK's Wi-Fi/RF area. [esp8266 4m2m.ld] The layout order is sketch, OTA, FS, EEPROM, Wi-Fi config. [esp8266 fs doc]
- Size chosen: the EEPROM sector is fixed (always 4 KB). The FS size comes from the board's "Flash Size" menu. [esp8266 fs doc]
- Re-upload: the "Erase Flash" menu has three choices. Only Sketch (no erase command), Sketch + WiFi Settings (`erase_region {rfcal_addr} 0x4000`) and All Flash Contents (`erase_flash`). [esp8266 boards.txt] "programming new sketch will not modify file system contents." [esp8266 fs doc]

**Inference**
- A power loss between erase and write loses all stored data. The same API with a 2-copy format would keep the user-facing contract and fix this.
- Users accepted `begin(size)`/`commit()`. It has been the de facto Arduino idiom on flash-only parts for about ten years.

### 1.3 RP2040 (arduino-pico): EEPROM and LittleFS

**Fact**
- EEPROM API: `begin(256..4096)` (rounded to 256), `commit()` and `end()`. It uses one 4 KB sector "at the end of flash space" and a RAM copy. [pico EEPROM doc] [pico EEPROM.cpp]
- `commit()`: skips if not dirty. Otherwise it disables interrupts (unless FreeRTOS), idles the other core, erases the 4 KB sector, programs it and resumes. There is no backup copy. [pico EEPROM.cpp]
- Wear warning: the emulation supports only "the number of writes as the onboard flash chip, not the 100,000 or so of a real EEPROM." [pico EEPROM doc]
- Location and size: a linker symbol `_EEPROM_start`. Even the "2MB (no FS)" menu entry gives a sketch length of 2 MB - 4096 (`flash_length=2093056`), so the EEPROM sector is always reserved. The FS size comes from the "Flash Size" menu, for example "2MB (Sketch: 1984KB, FS: 64KB)". [pico boards.txt] The FS range is 64 KB to 15 MB. [pico fs doc]
- Re-upload: "programming new sketch will not modify file system contents (or EEPROM data)." [pico fs doc] The upload is `picotool load … -f -x` or UF2. [pico platform.txt] `picotool erase` exists as a separate command (all, a partition, or a range). [picotool README]

**Inference**
- arduino-pico reserves 4 KB unconditionally instead of 0 B when unused. It is the simplest possible contract: a fixed location, a fixed size, survival across uploads, and no power-loss safety.

### 1.4 STM32duino: EEPROM emulation and the buffered API

**Fact**
- The default region is the last page or sector of flash, and its size equals that page or sector. Override macros are `FLASH_BASE_ADDRESS` and `FLASH_DATA_SECTOR`/`FLASH_PAGE_NUMBER`. Dual-bank parts may need manual settings. [stm32duino wiki] [stm32_eeprom.c]
- Format: one page. `eeprom_write_byte()` updates the RAM buffer and then calls `eeprom_buffer_flush()`, which erases and reprograms the whole page. So **every `EEPROM.write()` is one page erase**. The flush does no comparison with the stored data and keeps no backup copy. [stm32_eeprom.c]
- Buffered API: `eeprom_buffer_fill()`, `eeprom_buffered_read_byte()`, `eeprom_buffered_write_byte()` and `eeprom_buffer_flush()` batch many changes into one erase. The RAM buffer is `E2END+1` bytes. [stm32duino wiki] [stm32_eeprom.c]
- Pitfall seen in the code: a write before any read flushes a zero-initialized buffer over the other bytes, because filling happens only on read. [stm32_eeprom.c] Issue #2816 reports wrong erase parameters on G474 single-bank. (Found through a search result; not verified further.)
- Upload: the STM32CubeProgrammer wrapper erases only when `-e` is passed (`--erase all`), and its default passes no erase flag. [stm32CubeProg.sh] [stm32 platform.txt] FlashStorage_STM32's FAQ answers "Not with STM32" to whether an upload erases the data. [FlashStorage_STM32]

**Inference**
- STM32duino kept the AVR API unchanged. Because each byte write erases a page, the simple `EEPROM.write` loop wears the page quickly. The buffered API is the workaround, and it is non-standard.
- On parts whose last sector is very large (for example 128 KB sectors on F4), RAM equal to the sector is impractical. The size is not decoupled from the erase unit.

### 1.5 SAMD FlashStorage (cmaglie), a contrasting design

**Fact**
- API: `FlashStorage(name, Type)`, then `read()`/`write()`. There is also an EEPROM emulation with `commit()`. It warns of about 10,000 write cycles. On upload, the previous contents are "erased and filled with 0's". [FlashStorage SAMD]

**Inference**
- The storage is a const object inside the sketch image, so every upload rewrites it. This is the main known failure mode to avoid.

### 1.6 Adafruit nRF52: InternalFileSystem (LittleFS)

**Fact**
- LittleFS on internal flash. It takes 7 pages × 4 KB = 28 KB at 0xED000 on nRF52840 (0x6D000 on nRF52832). The block, read and prog sizes are 128 B. "Erase" is emulated by writing 0xFF over a 128 B block through a cached flash layer. If mounting fails, `begin()` erases the region and formats it. [nRF52 InternalFS]
- Pitfall: with the SoftDevice, flash operations are asynchronous. A BLE disconnect can make a write or erase fail undetected, which has caused corruption (a Meshtastic fork PR, proposed upstream). [Meshtastic nRF52 PR]

**Inference**
- The fixed region sits between the application and the bootloader, and DFU uploads write only the application, so it survives uploads.
- Silent auto-format on a mount error is a data-loss path that users meet as "settings vanished".

### 1.7 Arduino mbed (Portenta and others): KVStore and TDBStore

**Fact**
- API (KVStore): `set(key, buf, size, flags)`, `get`, `remove`, iterators, and flags such as write-once. TDBStore uses two areas, appends records to the active one, and on garbage collection "compact[s] all records from active area to the standby one". A RAM table rebuilt at init scans all records, and the maximum number of keys is configurable (the table is reallocated). A reserved area holds write-once data. [TDBStore.h] Erasing happens only on GC or reset. [mbed PR 11987]
- Power loss: "TBStore is also resilient to power failures." [Portenta KVS tutorial] It needs two copies, each a whole number of sectors and equal in size. [tronche TDBStore] It is recommended for "small, fixed-size data", with LittleFS for files. [mbed data architecture]
- Region: on Portenta the tutorial computes the region with `getFlashIAPLimits()`, which takes "the space already occupied by the sketch" into account and aligns to sectors. [Portenta KVS tutorial]

**Inference**
- If the start follows the sketch end, a larger sketch moves or overlaps the store, and data does not survive a sketch that grows. The safe place is the end of flash.

### 1.8 Zephyr: NVS, ZMS and the settings subsystem

**Fact**
- NVS: id-data pairs. Each sector holds data from the start and 8 B metadata (ATE) from the end. At least 2 sectors, with one always empty for GC. Data is written first and metadata second, and "data written in flash without metadata is ignored". An unchanged value is not rewritten. The docs give a lifetime formula, for example 2 × 1 KB sectors, 4 B written every minute and 20K erases give about 6.5 years. [Zephyr NVS]
- NVS finds free space by comparing flash to a uniform byte `erase_value` (`nvs_flash_cmp_const(..., erase_value, ...)`). [Zephyr nvs.c] `flash_parameters.erase_value` is a `uint8_t`. [Zephyr flash_parameters]
- ZMS: 16 B ATEs with crc8 and an 8-bit `cycle_cnt`. An ATE is valid only if its cycle count matches the sector's. "Every memory location is written only once for each cycle of sector write." It supports large write blocks and is aimed at RRAM/MRAM and large sectors. At least 2 sectors, and the partition should be about twice the data. An optional lookup cache costs 8 B per entry. [Zephyr ZMS] [Zephyr zms.c]
- Settings: string keys (`pkg/name`) with handlers (`h_set`, `h_commit`, `h_export`). Backends are NVS, ZMS, FCB or a file. It uses the devicetree partition labeled `storage` by default. [Zephyr settings]

**Inference**
- NVS's free-space test would misread 0xE339E339 erased flash unless the driver hides that pattern. ZMS's validity rule (CRC plus cycle count, write-once per cycle) is closer to what the 0xE339 parts need.
- Both need at least 2 erase units. On V003 the fast 64 B page would make that small, but ATE overhead per 64 B is large.

### 1.9 WCH and CH32-specific prior art

**Fact**
- openwch/arduino_core_ch32 EEPROM: tested only on V003. It stores 26 B in the option-byte / user-select-word area (2 data bytes plus 24 B) through a RAM buffer and `commit()`. It is ported from ch32fun's option-data example. [openwch EEPROM]
- ch32fun has examples `flashtest`, `optionbytes`, `optiondata`, `self_modify_code` and `bootload`. [ch32fun examples]
- WCH EVT flash examples were not examined in this survey (not found by search).

**Inference**
- The option-byte approach gives about 26 B without touching main flash and so survives any sketch-region erase. It is tiny, though, and may be cleared by a chip-wide erase or option-byte rewrite.

### 1.10 Other references (firmware, not cores)

**Fact**
- QMK wear-leveling EEPROM driver: the backing store defaults to 2048 B on MCU flash (8192 B on RP2040), and the logical size is half of it. "All wear-leveling drivers require an amount of RAM equivalent to the selected logical EEPROM size." [QMK EEPROM]
- Tasmota (ESP8266): a 4 KB settings struct with CRC32 and a `save_flag` counter, rotated over 7 flash sectors ("handles uploads"). A save happens only when the CRC differs. `SaveData` defaults to saving changes every second. Its quick-power-cycle counter clears 4-byte words in an erased sector without erasing ("Assume flash is default all ones…"). [Tasmota settings.ino] [Tasmota Commands]
- ESPHome: `flash_write_interval` defaults to 1 min "to prevent rapid changes … being quickly written to the flash". On ESP32 each preference is an NVS blob, and `sync()` skips unchanged blobs with `memcmp`. [ESPHome prefs] [ESPHome esp32 preferences.cpp]
- Marlin on STM32F1 flash emulation: two pages, both erased on each save (`M500`). The LPC1768 driver writes successive slots in a sector before erasing. (Found through search results on mirrors; not checked against upstream.)

**Inference**
- Mature firmware converges on the same three things: (a) the whole config as one struct with a CRC and a sequence number, (b) rotation across N slots for wear, (c) rate-limited or deferred writes.
- Tasmota's bit-clearing counter is the pattern that fails on 0xE339 parts.

---

## 2. Comparison table (same criteria)

| Candidate | API | Format | Min flash | RAM | Power loss | Wear leveling | Survives upload | Size chosen by | Known pitfalls |
|---|---|---|---|---|---|---|---|---|---|
| ESP32 Preferences/NVS | key-value, typed | log, 4 KB pages, bit-cleared state bitmap | 12 KB (3 pages); 20 KB default | ~22 KB/MB + per key | old value kept, the in-flight one may be lost | yes (append, ×126) | yes unless "Erase All" | partition table (menu) | fragmentation, NO_FREE_PAGES after layout change or unstable erase, needs rewrite-in-place |
| ESP32 EEPROM | Arduino EEPROM + begin/commit | one NVS blob | (NVS) | size + NVS | inherits NVS | inherits NVS | yes | NVS partition | writeBytes always dirty |
| ESP8266 EEPROM | Arduino EEPROM + begin/commit | 1 sector, erase-then-write | 4 KB fixed | size | **no** (erase window) | none | yes ("Only Sketch") | fixed in linker | power loss wipes data |
| arduino-pico EEPROM | same as ESP8266 | 1 sector, erase-then-write | 4 KB, always reserved | size | **no** | none | yes | fixed | IRQs off and other core idled during commit |
| arduino-pico LittleFS | FS | littlefs | 64 KB smallest menu | a few KB | yes (littlefs CoW) | dynamic | yes | "Flash Size" menu | — |
| STM32duino EEPROM | AVR EEPROM (+ buffered C API) | 1 page, erase per byte write | 1 page/sector | page size | **no** | none | yes (no `-e` by default) | macros | wears fast, a write before read clobbers, big sectors on some parts |
| SAMD FlashStorage | typed object | const in sketch image | object size | object | no | none | **no** (upload zeroes it) | the object | upload wipes |
| nRF52 InternalFS | FS | littlefs on 28 KB | 28 KB fixed | small | littlefs, but async flash failure under SoftDevice | dynamic | yes (inferred) | fixed | auto-format on mount error |
| mbed TDBStore | key-value | 2 areas, log + GC | 2 × sector | RAM table per key | yes ("resilient") | via GC | depends on region choice | code (FlashIAP limits) | region after the sketch moves as the sketch grows (inferred) |
| Zephyr NVS | id → blob | log per sector, data + 8 B ATE | 2 sectors | small (+ cache) | yes (metadata last) | yes | n/a (partition) | devicetree | assumes a uniform byte erase value |
| Zephyr ZMS | id → blob | log, 16 B ATE, cycle counter | 2 sectors (≈2× data) | 8 B/entry cache | yes | yes | n/a | devicetree | GC latency; format changes invalidate |
| Zephyr settings | string keys + handlers | over NVS/ZMS/FCB/file | backend | backend | backend | backend | n/a | devicetree | — |
| openwch EEPROM (V003) | EEPROM + commit | option bytes | 0 (26 B in the OB area) | 26 B | not stated | none | not stated | fixed | V003 only, 26 B |
| QMK wear_leveling | EEPROM | log over a backing store | 2 KB default | = logical size | not stated in docs | yes | — | config | — |
| Tasmota | struct | 4 KB struct + CRC, 7-slot rotation | 8 × 4 KB | 4 KB | the previous slot remains (inferred) | rotation | yes ("handles uploads") | fixed | QPC counter relies on bit-clearing |

---

## 3. Use cases: what users store, how big, how often

| Use case | Example | Size (fact where cited) | Write frequency | Source |
|---|---|---|---|---|
| Boot or restart counter | RNT `StartCounter` (Preferences) | 4 B | every boot | [RNT Preferences] |
| Wi-Fi credentials | RNT `ssid`/`password` | ≤32 + ≤64 B (inference: Wi-Fi limits) | once, at provisioning | [RNT Preferences] |
| Last output state | RNT GPIO `state` | 1 B | on each change | [RNT Preferences] |
| Provisioning parameters | WiFiManager: `mqtt_server[40]`, `mqtt_port[6]`, `api_token[34]` saved as JSON on SPIFFS | ~80 B raw, a few hundred B as JSON | only when the portal saves | [WiFiManager README] [AutoConnectWithFSParameters] |
| Sensor calibration | BNO055: `long` id + 11 × int16 offsets | 26 B | once after calibration (the example rewrites on every boot after calibrating) | [BNO055 restore_offsets] [Adafruit_BNO055.h] |
| Machine configuration | grbl: global settings at byte 1, parameters at 512, startup blocks at 768, build info at 942 of 1 KB EEPROM | ~1 KB | on user command | [grbl settings.h] |
| Device firmware config | Tasmota `TSettings` | 4 KB | changes saved every 1 s by default | [Tasmota settings.ino] [Tasmota Commands] |
| Light/switch state, energy totals | ESPHome | small per entity | rate-limited to 1 min | [ESPHome prefs] |
| Keyboard config/keymaps | QMK | 1-4 KB logical | on user change | [QMK EEPROM] |
| Analog value ring | AVR `eeprom_write` example | 1 B per 100 ms over all of EEPROM | ~10 writes/s | [AVR eeprom_write] |
| Logs / data logging | typically SD or a filesystem (LittleFS, FatFS) | KB-MB | continuous | inference: no library surveyed logs to the settings store |
| Keys/certificates | ESP32 Wi-Fi stack keeps credentials in NVS; TLS certificates are usually PEM in code or on a FS | 1-2 KB per certificate (inference) | rare | inference |

**Inference**
- Most real uses are small (tens of bytes to about 1 KB) and written rarely: at provisioning, after calibration, or on a user command. A struct-based "load at boot, save on change" pattern covers them.
- The high-frequency cases (counters, odometers, last state, energy totals) are where single-sector designs fail on wear and power loss. Mature firmware handles them with rate limiting (ESPHome 1 min, Tasmota 1 s plus rotation) or with append logs.
- Logs and certificates are filesystem-sized and belong to a different layer. On V003 (16 KB) they do not fit anyway.
- The AVR examples teach "write in a loop" (10 writes/s). Ported unchanged to a 10K-cycle flash with a commit per write, the flash lasts about 17 minutes. Ported with commit-only-when-asked (ESP8266/RP2040 style), the loop is harmless until someone adds `commit()` inside it. The API shape decides whether users fall into this trap.

---

## 4. How upload tools treat the storage region

| Platform | Default upload | Full-erase option | Source |
|---|---|---|---|
| ESP32 (esptool) | erases only the 4 KB sectors being written | IDE menu "Erase All Flash Before Sketch Upload" → `-e`; `erase-flash`, `erase-region` | [esptool] [esp32 boards.txt] |
| ESP8266 | "Only Sketch" (no erase command) | "Sketch + WiFi Settings" (erase 16 KB of RF/Wi-Fi), "All Flash Contents" (`erase_flash`) | [esp8266 boards.txt] |
| RP2040 arduino-pico | `picotool load -f -x` / UF2; FS and EEPROM untouched | `picotool erase` (all, a partition or a range); `flash_nuke.uf2` | [pico platform.txt] [pico fs doc] [picotool README] |
| STM32duino (CubeProgrammer) | no erase flag (only sectors written) | `-e` → `--erase all` | [stm32CubeProg.sh] |
| SAMD FlashStorage | the store is inside the image → overwritten | — | [FlashStorage SAMD] |
| nRF52 (Adafruit) | DFU writes only the app area (inference) | — | [nRF52 InternalFS] (region outside the app) |

**Inference**
- Every surveyed Arduino core with a separate storage region makes "upload keeps the data" the default and offers full erase as an explicit, labeled option (an IDE menu or a separate tool command). None erases the whole chip by default.
- The region is always at a fixed address that does not depend on sketch size: the end of flash, or a partition. The one surveyed scheme that ties it to the sketch end (the Portenta tutorial) is the exception.
- None of the surveyed Arduino cores lets the user pick the EEPROM size from a menu. ESP32 uses partition schemes, ESP8266 and RP2040 a fixed 4 KB sector, STM32 a fixed page or macros. Filesystem size, by contrast, is always a menu choice.

---

## 5. Cross-cutting observations (inference)

1. **Two contract families exist.**
   - (a) EEPROM-style byte array with explicit `commit()`: ESP8266, RP2040, ESP32 EEPROM, openwch V003 and FlashStorage EEPROM. It is cheap and familiar. On the surveyed cores it is not power-safe, because of single-sector erase-then-write.
   - (b) Key-value: ESP32 Preferences/NVS, mbed KVStore and Zephyr settings. It is power-safe and wear-leveled, but needs at least 2-3 erase units and more RAM or code.
2. **Power-loss safety is absent from every surveyed Arduino EEPROM emulation** (ESP8266, RP2040 and STM32duino all erase then write one sector). It is present in all key-value stores and filesystems. Offering it would be a differentiator, not a parity item.
3. **Wear handling is mostly delegated to the user** in EEPROM-style APIs, through documentation warnings. Key-value stores handle it by appending. Firmware handles it by rate limiting and slot rotation.
4. **Erase-value and write-once constraints.** NVS (ESP-IDF and Zephyr) and Tasmota-style counters assume 0xFF erase and bit-clearing. ZMS, littlefs and two-copy schemes with a CRC can be made independent of that. littlefs's design doc speaks of erased bits as 1s, so its behaviour with 0xE339 parts would need checking.
5. **The CPU or interrupts stall during flash operations elsewhere too.** arduino-pico disables interrupts and idles the other core during `commit()`, and nRF52 with SoftDevice makes flash asynchronous. The stall is commonly accepted and documented rather than hidden.
6. **Size selection.** EEPROM-like stores use a fixed size. Filesystems and partitions use a menu.
