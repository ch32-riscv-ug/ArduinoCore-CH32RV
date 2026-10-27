# PERSIST-01 survey: non-volatile storage in Arduino cores for small MCUs

Scope: how Arduino cores for parts with roughly ≤64 KB flash / ≤20 KB RAM provide non-volatile storage, measured mainly by **cost** (flash bytes, RAM bytes, reserved flash), and by what the core vs a library provides, how the storage area is reserved, and what the default upload does to it.

Method: the source code was read from shallow clones (commit hashes in the source list, cloned 2026-10-28). Issues were read via the GitHub API. No toolchains were available on this machine, so **no code size was measured**. Where a size is given it is either stated by the project or derived by arithmetic from the source (marked *inference*). Nothing here is a recommendation for CH32.

Notation: **F** = fact read from source / docs / issue (with link). **I** = inference (labelled).

---

## 1. Summary comparison

| # | Core / library (smallest part looked at) | API shape | Where it lives; raw flash API to sketches | Storage format | Flash used, and how it is reserved | RAM | Code / what it pulls in | Power loss, wear (what the code does) | Default upload erases it? |
|---|---|---|---|---|---|---|---|---|---|
| 1 | STM32duino, F030F4 (16K/4K), C011F4 (16K/6K), G030F6 (32K/8K) | AVR-style `EEPROM.read/write/update/get/put/[]`; plus C "buffered" API `eeprom_buffer_fill/flush`, `eeprom_buffered_read/write_byte` | Bundled library `libraries/EEPROM` (`src/utility/stm32_eeprom.c`). HAL flash driver is always compiled in the core (`HAL_FLASH_MODULE_ENABLED` mandatory) and callable | One flash page = whole EEPROM image; erase page then rewrite whole page from RAM copy | Last page: F030F4 1 KB, C011/G030 2 KB. Address computed in C (`FLASH_END+1-FLASH_PAGE_SIZE`); **not** reserved in linker (`LENGTH = upload.maximum_size` = full flash) | Static `eeprom_buffer[FLASH_PAGE_SIZE]`: 1 KB (F030), 2 KB (C011 = 1/3 of RAM, G030) | Not stated. Pulls `HAL_FLASHEx_Erase` / `HAL_FLASH_Program` | None: plain erase-then-program, no copy, no check. Byte API erases+rewrites the page on **every changed byte**; read API re-copies the whole page per byte | No: STM32CubeProgrammer is called without `-e`; full erase only via `platform.local.txt` |
| 1b | STM32duino L011/L031 (real data EEPROM) | same | same | Direct byte write to data EEPROM via `HAL_FLASHEx_DATAEEPROM_Program` | Separate data EEPROM; nothing reserved in flash | 0 B buffer | HAL data-EEPROM functions | Hardware EEPROM | I: no (separate memory, not in the .bin) |
| 2 | Roger Clark Arduino_STM32 (F103C8 64K/20K) | **Not** byte EEPROM: `EEPROM.read/write/update(uint16 addr, uint16 data)`, `init`, `format`, `count` | Bundled library `STM32F1/libraries/EEPROM` with its own `flash_stm32.c` (erase page / program halfword) | ST AN2594 two-page log: per page status + erase count, then 4-byte records {data16, vaddr16}; transfer to other page when full | 2 × 1 KB, at fixed `0x08000000+128K-2K` for **every** F1 board (header "HACK ALERT"); not reserved in linker; on a real 64 KB C8 this is outside nominal flash (issue #379) | ~16 B object (bases, size, status); no buffer | Not stated | Page states ERASED/RECEIVE/VALID and a recovery table in `init()`; erase counter per page; wear spread over 2 pages; no record checksum | Serial upload: yes (stm32flash without `-S/-e` does a mass erase). ST-Link/DFU: not verified |
| 3 | openwch arduino_core_ch32 (CH32V003 16K/2K) | ESP-style `EEPROM.begin()`, `read/write/get/put/[]`, `commit()`, `end()` | Bundled library `libraries/EEPROM` (contributed, "tested with CH32V003" only). Core compiles WCH SPL flash driver (`cores/arduino/ch32/lib/ch32yyxx_flash.c`), so `FLASH_*` SPL functions are callable | **Option bytes** (user-selected word area, 64 B): data0/data1 + 24 more halfwords, each byte stored with its inverse → 26 bytes | 0 B of code flash (separate option-byte block) | `new uint8_t[26]` on heap + object (~12 B) + 16 B stack in `commit()` | Not stated. Uses `new` (I: pulls malloc if not already linked) | Erase whole option block, rewrite saved RDPR/USER/WRPR + data; power loss mid-way could leave RDPR/USER/WRPR erased (I) | I: upload via OpenOCD `program` / wchisp touches code flash, not option bytes (not verified) |
| 4 | ch32fun (cnlohr) | None (no EEPROM class) | Register-level examples only (`examples/optiondata`, `examples/flashtest`); `extralibs/ch20x_30x_flash.h` helpers for V20x/V30x only | Example-defined | Example uses a fixed address; nothing reserved | 0 (example code) | Example code is a few dozen lines of register writes | none | minichlink `-w` erases only the sectors it writes (skips already-erased) |
| 5 | Air001 (AirMCU) / py32duino (PY32F002A 20K/3K, F030x4 16K/2K) | STM32duino copy (same API) | Bundled library, file renamed `air_eeprom.c`; vendor HAL flash | One page, erase + page-program from RAM copy | Last page = **128 B**; not reserved in linker | 128 B buffer | HAL flash (page program) | Same as STM32duino | **Yes**: AirISP is called with `write_flash -e` = erase all |
| 6 | CommunityGD32Cores ArduinoCore-GD32 (GD32F330F4 16K/4K) | ESP-style `begin/read/write/get/put/commit` | Two bundled libraries: `FlashStorage` (template) + `EEPROM` wrapper; uses SPL `fmc_*` | Whole area erased and rewritten from RAM | Default **4 KB** at end of `upload.maximum_size`; not reserved (16 KB board → last 4 KB of a 16 KB linker region) | Default **4 KB** RAM buffer (all of F330F4's RAM; issue #140) | Not stated | none; page size hard-coded 2 KB (<512 KB) | I: OpenOCD `program` erases only written sectors (not verified) |
| 7 | Teensy LC (MKL26Z64, 62K+2K / 8K) | `avr/eeprom.h` functions in the core (`eeprom_read_byte` …) under the `EEPROM` library | **Core** (`teensy3/eeprom.c`) | Log of 16-bit entries {offset8, value8} appended; when full, rebuild into 2 erased sectors | 2 KB (0xF800–0xFFFF), **reserved by linker**: FLASH LENGTH = 62K | 2 B static; compaction uses a 128-B stack buffer (E2END = 0x7F) | Not stated | Append = 1 longword program; compaction erases both sectors then rewrites from RAM → loss window (I); no checksum | Not determinable from source (closed bootloader) |
| 8 | LGT8F328P (lgt8fx, 32K/2K) | AVR `EEPROM` API + `lgt_eeprom_*` C API | Bundled library `E2PROM`; hardware does the emulation | **Hardware** page-swap emulation: 1 KB EEPROM uses 2 KB flash, 1020 B usable | 0x7800–0x7FFF; reserved by `upload.maximum_size=29696` (= bootloader start 0x7400) | none | Register access like AVR (small) | Hardware page swap on every write (per the header comment) | Header comment says "erased each time the sketch will be uploaded"; avrdude is called with `-D` |
| 9 | Small AVR (MiniCore, megaTinyCore) | `EEPROM.read/write/update/get/put/[]` | Library `EEPROM` = header only, inline | Real EEPROM | none | 0 | megaTinyCore: write is 13 inline asm instructions; MiniCore: avr-libc `eeprom_*` | Hardware; docs recommend BOD | No (EESAVE fuse menu on MiniCore; `-D`) |
| 9b | Optiboot/urboot `Flash.h` (megaTinyCore, MiniCore) | `Flash flash(space, size, ram_buf, ram_size)`, `put/get/write_page` | Library; needs bootloader that exposes SPM | Sketch-declared page-aligned `const` array in program flash | Inside the program image (array) | One page buffer (64–256 B) chosen by sketch | Bootloader does the SPM | none | Yes (array is part of the hex) (I) |
| 10 | Energia MSP430 `MspFlash` | `Flash.erase/read/write(ptr…)` only | Bundled library | Raw segment access; info segments B–D (64 B each) or a sketch array | Info memory (not in main flash) or a sketch-declared array | 0 | ~90 lines of register code | none | Not verified (mspdebug `prog`) |
| 11 | MattairTech SAMD10/11 core | **none** in the core | – | – | – | – | – | – | – |
| 11b | cmaglie `FlashStorage` (SAMD, external) | `FlashStorage(name,T)`, `read/write`; `FlashAsEEPROM` with `commit()` | External library | Sketch-declared aligned `const` array in program flash | Inside program image | FlashAsEEPROM: 1 KB + 1 B by default | Not stated | none | **Yes** (README: erased and filled with 0 each upload) |

Cores that offer **no storage at all** on the smallest parts: MattairTech SAMD10/11 core (no EEPROM/flash library in `libraries/`); ch32fun (no abstraction, examples only). openwch's library is V003-only by its own statement (wrong layout on V103, issue #213).

---

## 2. STM32duino (stm32duino/Arduino_Core_STM32)

### Facts
- **Location.** The emulation is in the bundled EEPROM library, `libraries/EEPROM/src/utility/stm32_eeprom.c` / `.h`, not in `cores/` (in 2019 it was `cores/arduino/stm32/stm32_eeprom.c`, see the link in issue #428). `EEPROM.h` is the Christopher Andrews AVR API (`read/write/update/get/put/[]/length`).
- **Address choice.** `EEPROM_FLASH_PAGE_NUMBER = (LL_GetFlashSize()*1024 / FLASH_PAGE_SIZE) - 1` (stm32_eeprom.c L57–64), `FLASH_END` from that (L66–92), `FLASH_BASE_ADDRESS = FLASH_END + 1 - FLASH_PAGE_SIZE` (L95–109). All are `#ifndef` and can be overridden (e.g. `build_opt.h`). `LL_GetFlashSize()` reads the device flash-size register at run time.
- **Size = one page.** `E2END = FLASH_PAGE_SIZE - 1` for parts without data EEPROM (stm32_eeprom.h L104–106). Page sizes in the HAL: F030x6 (F030F4) 0x400 (stm32f0xx_hal_flash_ex.h L143–146), C0 0x800 (stm32c0xx_hal_flash.h L731), G0 0x800 (stm32g0xx_hal_flash.h L888), L0 128 B.
- **RAM.** `static uint8_t eeprom_buffer[E2END + 1] __attribute__((aligned(8)))` (stm32_eeprom.c L111–113) → 1 KB on F030F4 (4 KB RAM), 2 KB on C011F4 (6 KB RAM), 2 KB on G030F6 (8 KB RAM).
- **Write path.** `eeprom_write_byte()` = write into buffer, then `eeprom_buffer_flush()` (L141–155). `flush` erases the page (`HAL_FLASHEx_Erase`, NbPages=1) and programs the whole buffer word/doubleword at a time (L227–373). On a program error it just stops (`address = address_end + 1`).
- **Read path.** `eeprom_read_byte()` calls `eeprom_buffer_fill()` (memcpy of the whole page into RAM) and then returns one byte (L120–133). So every `EEPROM.read()` copies 1–2 KB.
- **`put()`** loops over bytes calling `EERef::update()` (EEPROM.h L112–115, L253–261) → one full-page erase+program per **changed** byte. Issue #2160 ("EEPROM library flushes data on each write") reports this; PR #2163 (flags to avoid re-reads/erases) was closed and is not in the current code.
- **Buffered API.** `eeprom_buffer_fill()`, `eeprom_buffered_read_byte()`, `eeprom_buffered_write_byte()`, `eeprom_buffer_flush()` are exported (stm32_eeprom.h L116–121) so a sketch can fill once, edit, and flush once (example in issue #2173).
- **Real data EEPROM (L0, L1).** If `DATA_EEPROM_BASE` exists, read is a direct load and write is `HAL_FLASHEx_DATAEEPROM_Unlock/Program(BYTE)/Lock` per byte; no RAM buffer (L122–150).
- **Linker.** `FLASH LENGTH = LD_MAX_SIZE - LD_FLASH_OFFSET` (e.g. variants/STM32F0xx/F030F4P/ldscript.ld L40) and `LD_MAX_SIZE = upload.maximum_size` (platform.txt L78). F030F4 / C011F4 `maximum_size=16384`, G030F6 `32768` (boards.txt L2645, L2147, L7591) = full flash. The last page is **not** reserved; a large sketch overlaps the EEPROM page (stated by a contributor in issue #428).
- **HAL flash driver.** `HAL_FLASH_MODULE_ENABLED` is in the "Mandatory HALv1 modules" list (cores/arduino/stm32/stm32yyxx_hal_conf.h L158–165), so it is always compiled; `-Wl,--gc-sections` is used (platform.txt L78).
- **Upload.** Board menu options pass `-a -s -f -n …` but no `-e` (boards.txt L3583–3601); `stm32CubeProg.sh` only adds `--erase all` when `-e` is given (Arduino_Tools stm32CubeProg.sh L15, L217–218). Maintainer in issue #428: "Since #514, by default there is no full erase"; full erase via `platform.local.txt`.
- **Issues.** #428 EEPROM erased on upload (old behaviour, fixed by #514); #2160 flushes on each write; #2173 EEPROM data "lost" after core update 2.5.0→2.6.0 (address calculation changed when H5 was added); #1500 F103C8 `FLASH_BASE_ADDRESS` wrong value; #1860 G0B1 cannot save; #2816 G474 erase parameters wrong.

### Inference
- I: the flash cost of the library on F0 is dominated by `HAL_FLASHEx_Erase` + `HAL_FLASH_Program` + wait/err handling from the HAL; not measured here.
- I: STM32CubeProgrammer `--download` without erase erases only the sectors covered by the binary, so the last page survives an upload as long as the sketch does not reach it.

---

## 3. Roger Clark Arduino_STM32 (STM32F1) EEPROM library

### Facts
- **Location.** `STM32F1/libraries/EEPROM/` (EEPROM.cpp 585 lines, flash_stm32.c 160 lines with its own `FLASH_ErasePage`, `FLASH_ProgramHalfWord`). Comment points to ST AN2594 (EEPROM.cpp L2).
- **API is 16-bit virtual-address / 16-bit value**, not the AVR byte API: `init()`, `init(base0, base1, pageSize[, pages])`, `format`, `read(addr)`, `read(addr,&data)`, `write`, `update`, `count`, `maxcount`, `erases` (EEPROM.h L57–91).
- **Address.** `#define MCU_STM32F103RB` is hard-coded ("HACK ALERT … may not match your processor", EEPROM.h L7–9) → page size 1 KB and `EEPROM_START_ADDRESS = 0x08000000 + 128K - 2*1K` for every board (L11–31). Issue #379: on a 64 KB part this is outside flash, on >128 KB parts it can overlap code; maintainer: "virtually all F103C8's have 128k"; `init()` with explicit addresses is the workaround. Issue #922: does not work on F103C6 (32 KB) even with `init(0x8007C00, 0x8008000, 0x400, 1)`.
- **Linker.** jtag_c8.ld `rom LENGTH = 64K` (variants/generic_stm32f103c/ld/jtag_c8.ld L26); no reservation.
- **Format.** Page header: halfword status (0xFFFF erased, 0xEEEE receive, 0x0000 valid) + halfword erase counter at +2 (EE_ErasePage L31–49). Records are 4 bytes {data, vaddr} appended (EE_VerifyPageFullWriteVariable L214–284). Same value → no write; if the latest record's data halfword is still 0xFFFF it is programmed in place. When full, a new page is marked RECEIVE, the new value written, live variables copied (EE_PageTransfer L135–200), old page erased, new page marked VALID. Capacity `maxcount() = PageSize*Pages/4 - 1` = 255 variables for 1 KB (L580).
- **Recovery.** `init()` (L313–407) handles every page-status pair (incl. RECEIVE+VALID → finish transfer). No record checksum.
- **Read** scans the valid page backward for the address (L482–509).
- **RAM.** Object members `PageBase0, PageBase1, PageSize` (uint32), `Pages, Status` (uint16) (EEPROM.h L76–80); no buffer.
- **Upload.** `serial_upload` runs `stm32flash -g 0x8000000 -b 115200 -w file` (tools/linux/serial_upload). In stm32flash's `main.c` (L236–240 of the bundled source) no `-S`/`-e` → `num_pages = 0xff /* mass erase */`. ST-Link: `st-flash write file 0x8000000` (tools/linux/stlink_upload). Default menu entry for generic F103C is the DFU (STM32duino bootloader) method (boards.txt L363).
- **Issues.** #379, #922 (above); #751 return code mismatch; #606 "multiple data at the same address".

### Inference
- I: per-entry overhead is 100 % (2 B address per 2 B value), so 1 KB pages hold 255 values.
- I: st-flash `write` erases only the pages it writes; not verified here.

---

## 4. openwch/arduino_core_ch32

### Facts
- **Location.** Bundled library `libraries/EEPROM` (author maxint-rd, PR #103). library.properties: "uses the option bytes page … limited to 26 bytes. Tested with CH32V003." PR #103: "not tested with or developed for other CH32 chips".
- **API.** ESP8266-style: `begin()`, `read/write`, `get/put` (memcmp before setting `_dirty`), `operator[]` (sets `_dirty` on any access through `getDataPtr()`), `erase()`, `commit()`, `end()`, `length()`; extra `ReadOptionBytes()` (EEPROM.h L60–106).
- **Format.** `_size = 26` (EEPROM.cpp L47): data0/data1 (OB halfwords 2,3) + OB halfwords 8..31 (`OB_AVAIL_DATA_START 8`, "valid for CH32V003; how about others?" L27). Each halfword stores the byte and hardware-generated inverse.
- **commit()** (L96–156): saves RDPR/USER/WRPR0..3 + new data0/1 into `volatile uint16_t hold[8]` on the stack, unlocks with KEYR+OBKEYR, `OPTER` erase of the whole 64-B block, writes the 8 held halfwords back, then each data byte that is not 0xFF. No error checks, no timeouts. Only writes when `_dirty`.
- **RAM.** `_data = new uint8_t[_size]` (L53) on heap; `end()` sets `_data = nullptr` without `delete` (L158–165).
- **Raw flash.** SPL flash drivers are in `system/<series>/SRC/Peripheral/src/*_flash.c` and wrapped into the core by `cores/arduino/ch32/lib/ch32yyxx_flash.c`; e.g. V00x has `FLASH_Unlock_Fast`, `FLASH_ErasePage_Fast`, `FLASH_ProgramPage_Fast`, `FLASH_BufLoad` (ch32v00x_flash.c L708–800). Compile flags include `-ffunction-sections -fdata-sections` (platform.txt L37); link uses `--gc-sections` with the core archive under `--whole-archive` (platform.txt L135).
- **Linker.** V00x Link.ld `FLASH LENGTH = 16K` (system/CH32V00x/SRC/Ld/Link.ld L9); no reservation (not needed for option bytes).
- **Upload.** OpenOCD `program {elf} verify; wlink_reset_resume` (platform.txt L170); wchisp `flash {elf}` (L180).
- **Issues.** #213: on CH32V103 only the first 2 bytes work, the other 24 always read 255; reporter writes own last-128-B emulation using `FLASH_ROM_ERASE/WRITE`, which are missing from the core's old SPL. Maintainer of the lib notes problems also on X033/V002/V006 and flash-latency settings. PR #247 (EEPROM for V20x) closed unmerged. #63 asks for an ESP-like emulated EEPROM.

### Inference
- I: `new` makes the library depend on the heap allocator; on a sketch that otherwise has no `malloc`, that adds newlib-nano malloc code (size not measured).
- I: a power cut between the OB erase and the rewrite of RDPR/USER would leave option bytes erased (on V003 an erased RDPR byte means read protection per WCH convention; not tested here).
- I: neither upload tool is told to touch option bytes, so the 26 bytes survive uploads; not verified.

---

## 5. ch32fun (cnlohr)

### Facts
- No EEPROM / storage API in `ch32fun/ch32fun.c|h` (grep for flash write helpers finds only latency setup).
- `examples/optiondata/optiondata.c`: the 2-byte data0/data1 pattern openwch copied (hold[6], OPTER erase, OPTPG rewrite; README: "no timeouts … to reduce the code footprint").
- `examples/flashtest/flashtest.c`: V003 fast page erase (`CR_PAGE_ER`, ~3 ms) and 64-B page program via `BUF_RST`/`BUF_LOAD`/`STRT` (~3 ms) at a fixed address 0x08003700, with cycle counts printed.
- `extralibs/ch20x_30x_flash.h`: unlock/lock, `ch20x_30x_flash_cmd_read/write/erase/verify` for V20x/V30x only.
- **Upload.** `minichlink -w file flash -b` (ch32fun.mk L427). `DefaultWriteBinaryBlob` erases a sector only if it is not already erased and only the sectors it writes (minichlink.c L2245–2262, L2347). `-E` is an explicit whole-chip erase (L331).

### Inference
- I: since the default write only erases sectors it writes, data placed beyond the end of the image survives a ch32fun upload.

---

## 6. Puya PY32 / Air001

### Facts
- **AirMCU** (Air-duino/Arduino-AirMCU) and **py32duino** (py32duino/Arduino-PY32) ship the STM32duino EEPROM library almost unchanged as `libraries/EEPROM/src/utility/air_eeprom.c`; the two copies differ only in the LL include name.
- Air001 / PY32 HAL: `FLASH_PAGE_SIZE = 0x80` (Air001-Drivers air001_dev.h L473) → EEPROM = **128 B** = 128 B RAM buffer. Erase uses `FLASH_TYPEERASE_PAGEERASE` and programs with `HAL_FLASH_Program(FLASH_TYPEPROGRAM_PAGE, …)` 128 B at a time (air_eeprom.c L239–256).
- Linker: `FLASH LENGTH = LD_MAX_SIZE - LD_FLASH_OFFSET` (variants/AIR001/AIR001_DEV/ldscript.ld L33); Air001 `maximum_size=32768`; py32 PY32F002Ax5 `20480/3072`, PY32F030x4 `16384/2048` (py32 boards.txt L492–493, L185–186). Not reserved.
- **Upload.** `AirISP … write_flash -e 0x08000000 file.hex` (AirMCU platform.txt L179); py32duino `write_flash -e -p …` (L179). In AirISP, `-e` is the alias of `--erase-all`: "Erase all sectors on flash before writing (default only erase sectors to be written)" (AirISP Program.cs L88–89).
- **Issues.** AirMCU #95: user's EEPROM examples read 255 after re-upload; maintainer: "the chip is erased before burning, every burn restores it". #92: only 128 bytes; the reply says the size is a multiple of the 128-B unit, and that `eeprom_write_byte`'s data-EEPROM HAL function is undefined when `DATA_EEPROM_*` is defined manually.

---

## 7. GD32 (CommunityGD32Cores/ArduinoCore-GD32)

### Facts
- Two bundled libraries: `FlashStorage` (template `FlashStorage<size, fmc_end = ARDUINO_UPLOAD_MAXIMUM_SIZE>`) and `EEPROM` (`static EEPROMClass<EEPROM_EMULATION_SIZE> EEPROM;`, default **4096**) (EEPROM/src/EEPROM.h L33–37). README: storage "defaults to being at the end of the flash, for which we can reserve space externally" — designed for the Kaleidoscope keyboard firmware.
- `FlashStorage` holds `uint8_t buffer_[_storage_size]` (FlashStorage.h L42); `static_assert(size % 4096 == 0)` (L38–39); `begin()` copies flash→RAM; `commit()` erases each page and word-programs all of it (L99–116), with `pageSizeForAddress()` returning 2048 for addresses below 512 KB (L49–55). `update()` = `write()` (FlashAsEEPROM.h L63–66). No dirty flag.
- `ARDUINO_UPLOAD_MAXIMUM_SIZE` comes from `upload.maximum_size` (platform.txt L59). GD32F330F4: `maximum_size=16384`, `maximum_data_size=4096` (boards.txt L1081–1082); linker FLASH 16K / RAM 4K (variants/GD32F330F4_GENERIC/ldscript.ld L6–7) → storage = last 4 KB of a region the linker also gives to code.
- Upload: OpenOCD `program {elf} verify reset` (platform.txt L121, L129, L136), dfu-util `-D bin` (L145), serial_upload.
- **Issues.** #140 (open): "EEPROM uses too much RAM; low-end MCUs lack sufficient memory". PR #134 (open): after power cycle flash still has the old value; fix clears FMC error flags.

### Inference
- I: on GD32F3x0 (1 KB flash pages) the hard-coded 2048-B step means every other page is programmed without its own erase.

---

## 8. Teensy LC (PaulStoffregen/cores, teensy3/eeprom.c)

### Facts
- Code is in the **core**: `teensy3/eeprom.c` implements avr-libc-style `eeprom_read_byte/word/dword/block`, `eeprom_write_*`; the `EEPROM` class is the separate Teensyduino library on top.
- **Size.** `E2END = 0x7F` for MKL26Z64 (avr/eeprom.h L38–39), `EEPROM_MAX 255` (eeprom.c L59–60) → 128 B usable.
- **Region.** `FLASH_BEGIN 63488` … `FLASH_END 65536` (eeprom.c L400–401) = 2 KB = two 1 KB sectors. Linker `FLASH LENGTH = 62K` (mkl26z64.ld L33) → **reserved by the linker**.
- **Format.** 16-bit entries, low byte = offset, high byte = value, appended (L454–478). `eeprom_initialize()` finds the first 0xFFFF halfword (L404–415); `eeprom_read_byte()` scans the whole log and keeps the last match (L417–436). Halfwords are written by programming the containing longword with 0xFFFF in the other half, so each longword is programmed twice (L465–477).
- **Compaction** when the log reaches the end: build `uint8_t buf[EEPROM_SIZE]` on the stack from the log, **erase both sectors**, write back non-0xFF bytes (L479–515). The flash command runs from a RAM copy on the stack (`do_flash_cmd[]`, 7 halfwords) with IRQs disabled.
- **RAM.** `static uint16_t flashend` (2 B) + 128 B stack during compaction + 14 B stack for the RAM routine.

### Inference
- I: 2 KB / 2 B = 1024 entries per erase cycle of both sectors.
- I: a power cut between the erase and the rewrite in compaction loses everything; a torn entry is not detectable (no checksum).
- Upload: Teensy Loader / HalfKay is closed source; not determinable from this repo.

---

## 9. LGT8F328P (dbuezas/lgt8fx)

### Facts
- Bundled library `lgt8f/libraries/E2PROM` (EEPROM.h 427 lines, EEPROM.cpp 385 lines). Provides `lgt_eeprom_*()` C API and the standard Arduino `EEPROM` API (and optionally the old LGT API).
- **Hardware emulation.** Header comment (EEPROM.h L78–107): page swapping is done by the device; each 1 KB of EEPROM uses 2 KB of main flash; 1020 B usable per 1 KB; size selectable 1/2/4/8 KB via `ECCR` (`lgt_eeprom_init()`, EEPROM.cpp L5–34); writes go through `EEARL/EEARH/EEDR/EECR` like AVR (L180–195). Constructor calls `lgt_eeprom_init(1)` (EEPROM.h L381).
- Same comment: "the content of the emulated EEPROM will be erased each time the sketch will be uploaded (even if it's an update)" (L106–107).
- **Reservation.** `328.upload.maximum_size=29696` (boards.txt L19) = 0x7400. PR #261 discussion: factory bootloader starts at 0x7400, emulated EEPROM at 0x7800 to end; a different EEPROM size needs a different bootloader. PR #261 (menu for EEPROM size) was closed unmerged.
- Upload: avrdude `-D -Uflash:w:…` (platform.txt L105).
- **Issues.** #254 (open) asks for an AN2594-style log; reply: LGT has 2 KB RAM and the swap is in hardware; SWM (serial write mode) can write 32-bit cells without a swap.

### Inference
- I: the core's own cost is register writes (tens of bytes); the flash cost is the fixed 2 KB + the 1 KB bootloader the board already excludes.

---

## 10. Small AVR: MiniCore, megaTinyCore (baseline)

### Facts
- **EEPROM library** in both cores is a single header `libraries/EEPROM/src/EEPROM.h`. MiniCore uses avr-libc `eeprom_read_byte/eeprom_write_byte` (EEPROM.h L25, L42–47). megaTinyCore writes with an inline asm sequence of 13 instructions that does the NVMCTRL erase-write of one byte (EEPROM.h L74–95); README: 64 B–256 B EEPROM, 4 ms per byte erase+write, 100 k cycles; recommends BOD for power-loss safety and suggests flash for panic dumps (README L39–49, L176–199).
- **Upload.** MiniCore avrdude `-D … -Ueeprom:w:{.eep} -Uflash:w:{.hex}` (platform.txt L131); an "EEPROM retained / not retained" menu sets the EESAVE fuse (boards.txt L55–59). megaTinyCore README: EESAVE fuse; with Optiboot EEPROM is not erased.
- **USERSIG** (megaTinyCore): user row 32–64 B, not erased by chip erase; identical API to EEPROM on tinyAVR (USERSIG/README.md L8–25).
- **Self-programming flash.** `Optiboot_flasher` (megaTinyCore) / `Flash` (MiniCore, urboot): sketch declares `const uint8_t flashSpace[SPM_PAGESIZE*N] __attribute__((aligned(SPM_PAGESIZE))) = {}` and a RAM buffer of one page (examples, e.g. Flash_put_get.ino L31–35, MiniCore Flash_get_put.ino L33–40); the bootloader performs SPM. README: "the bootloader section … they have 36 bytes left".

### Inference
- I: AVR users expect EEPROM to cost near 0 RAM and tens of bytes of flash per call site, and to survive uploads.
- I: with the Flash libraries the data array is part of the hex, so a re-upload overwrites it with zeros.

---

## 11. Energia MSP430 (MspFlash)

### Facts
- Bundled library `libraries/MspFlash` (MspFlash.h, 89-line .cpp): `Flash.erase(ptr)`, `Flash.read(ptr, dest, len)`, `Flash.write(ptr, src, len)` only; register writes `FCTL1/2/3` with WDT disabled (MspFlash.cpp L30–77).
- Header comment: info segments B–D (64 B each on FLASH2 parts, 0x1000…) are recommended; main flash use needs either a linker change or a static `const` array of ≥2 segments and `SEGPTR()` to find a whole 512-B segment inside it (MspFlash.h L35–49, L82). SEGMENT_A (calibration) is excluded.
- Upload: mspdebug `--force-reset "prog file.hex"` (platform.txt.template L107) or DSLite `load` (L101).

### Inference
- I: whether `prog` preserves information memory depends on the mspdebug driver's erase type; not verified.

---

## 12. SAMD small parts

### Facts
- MattairTech ArduinoCore-samd (SAMD10/11/21…): `libraries/` contains HID, I2S, SAMD_AnalogCorrection, SPI, USBHost, Wire — **no EEPROM or flash storage library**; README mentions only an external SPI EEPROM library. Last commit 2018.
- The commonly used external library is **cmaglie/FlashStorage**: `FlashStorage(name, T)` expands to a `static const uint8_t _data[...] __attribute__((aligned(256))) = {}` in program flash (FlashStorage.h L39–47); `write(T data)` takes T by value, erases then writes (L77). `FlashAsEEPROM`: RAM copy `struct { byte data[EEPROM_EMULATION_SIZE]; boolean valid; }` with default size **1024** (FlashAsEEPROM.h L27–34), `commit()` writes only when dirty (FlashAsEEPROM.cpp L65–72). readme: "every time you upload a new sketch, the previous content of the FlashStorage is erased and filled with 0's" (readme.md L81–83).

### Inference
- I: on a SAMD D11C14 (16 KB / 4 KB) the default FlashAsEEPROM costs a quarter of RAM; `read()`/`write()` by value may add another T-sized stack copy.

---

## 13. Cross-cutting observations (facts collected above, grouped)

- **Reservation methods seen**
  - Linker region shortened: Teensy LC (62K), LGT8 (via `upload.maximum_size` = bootloader start).
  - Fixed address at the last page(s), no linker reservation: STM32duino, Air001/PY32, GD32 FlashStorage, Roger Clark (fixed 124K offset).
  - Array inside the program image: cmaglie FlashStorage, AVR Optiboot/urboot Flash, MspFlash's `SEGPTR` option → always overwritten by upload.
  - Separate memory: option bytes (openwch V003), data EEPROM (STM32L0), info memory (MSP430), USERROW (tinyAVR).
- **RAM**
  - Full-page mirror: STM32duino 1–2 KB, Air001/PY32 128 B, GD32 4 KB, SAMD FlashAsEEPROM 1 KB, openwch 26 B.
  - No mirror: Roger Clark log (~16 B), Teensy LC log (2 B static + stack at compaction), AVR/LGT hardware.
- **Default upload erases the area**: Air001/PY32 (AirISP `-e`), Roger Clark serial (stm32flash mass erase), all in-image array schemes, LGT8 (per its header comment). Does not: STM32duino (since #514), ch32fun minichlink, AVR with EESAVE.
- **Power-loss handling in code**: only the Roger Clark AN2594 port has page states + recovery. All page-mirror schemes erase then program with no copy. Teensy LC and LGT8 rely on log/hardware swap but have no checksum in the code read.

---

## 14. Sources (commit used)

- stm32duino/Arduino_Core_STM32 @ `8bc8cc90b23975fb6722ef095780af16e6837f02`
  - https://github.com/stm32duino/Arduino_Core_STM32/blob/8bc8cc90b23975fb6722ef095780af16e6837f02/libraries/EEPROM/src/utility/stm32_eeprom.c
  - …/libraries/EEPROM/src/utility/stm32_eeprom.h, …/libraries/EEPROM/src/EEPROM.h
  - …/cores/arduino/stm32/stm32yyxx_hal_conf.h, …/variants/STM32F0xx/F030F4P/ldscript.ld, …/platform.txt, …/boards.txt
  - …/system/Drivers/STM32F0xx_HAL_Driver/Inc/stm32f0xx_hal_flash_ex.h, STM32C0/G0/L0 `*_hal_flash.h`
  - Issues: [#428](https://github.com/stm32duino/Arduino_Core_STM32/issues/428), [#2160](https://github.com/stm32duino/Arduino_Core_STM32/issues/2160), [#2163](https://github.com/stm32duino/Arduino_Core_STM32/pull/2163), [#2173](https://github.com/stm32duino/Arduino_Core_STM32/issues/2173), [#1500](https://github.com/stm32duino/Arduino_Core_STM32/issues/1500), [#1860](https://github.com/stm32duino/Arduino_Core_STM32/issues/1860), [#2816](https://github.com/stm32duino/Arduino_Core_STM32/issues/2816)
- stm32duino/Arduino_Tools @ `02ad74adb8db6a38fb66003682e3c96789c04f29` — stm32CubeProg.sh
- rogerclarkmelbourne/Arduino_STM32 @ `78d0cd817a8caf5a43018afc181eb24db5a1b66f`
  - https://github.com/rogerclarkmelbourne/Arduino_STM32/blob/78d0cd817a8caf5a43018afc181eb24db5a1b66f/STM32F1/libraries/EEPROM/EEPROM.cpp (and EEPROM.h, flash_stm32.c)
  - …/STM32F1/variants/generic_stm32f103c/ld/jtag_c8.ld, …/tools/linux/serial_upload, …/tools/linux/stlink_upload, …/tools/linux/src/stm32flash_serial/src/main.c
  - Issues: [#379](https://github.com/rogerclarkmelbourne/Arduino_STM32/issues/379), [#922](https://github.com/rogerclarkmelbourne/Arduino_STM32/issues/922), [#751](https://github.com/rogerclarkmelbourne/Arduino_STM32/issues/751), [#606](https://github.com/rogerclarkmelbourne/Arduino_STM32/issues/606)
- openwch/arduino_core_ch32 @ `d76716239cdf8a084a5045c3dfd3151b3f69eeec`
  - https://github.com/openwch/arduino_core_ch32/blob/d76716239cdf8a084a5045c3dfd3151b3f69eeec/libraries/EEPROM/src/EEPROM.cpp (and EEPROM.h, library.properties)
  - …/system/CH32V00x/SRC/Peripheral/src/ch32v00x_flash.c, …/cores/arduino/ch32/lib/ch32yyxx_flash.c, …/system/CH32V00x/SRC/Ld/Link.ld, …/platform.txt
  - Issues: [#213](https://github.com/openwch/arduino_core_ch32/issues/213), [#103](https://github.com/openwch/arduino_core_ch32/pull/103), [#247](https://github.com/openwch/arduino_core_ch32/pull/247), [#63](https://github.com/openwch/arduino_core_ch32/issues/63)
- cnlohr/ch32fun @ `84799bb3ae73cf7c7e54a7126dbb21b12de6eab2` — examples/optiondata, examples/flashtest, extralibs/ch20x_30x_flash.h, ch32fun/ch32fun.mk, minichlink/minichlink.c
- Air-duino/Arduino-AirMCU @ `0e97a0fad2ac5b4fb298439842f03596e9d9d225` — libraries/EEPROM/src/utility/air_eeprom.c, platform.txt, boards.txt; Air-duino/Air001-Drivers (HEAD at clone) CMSIS/Device/AIR001xx/Include/air001_dev.h; Air-duino/AirISP @ `171e839503fee28c26337eb1541e8505f6b887a5` AirISP/Program.cs
  - Issues: [#92](https://github.com/Air-duino/Arduino-AirMCU/issues/92), [#95](https://github.com/Air-duino/Arduino-AirMCU/issues/95)
- py32duino/Arduino-PY32 @ `5157e0a8c2df296b348f873bc53d86bfeb3b3178` — libraries/EEPROM, platform.txt, boards.txt
- CommunityGD32Cores/ArduinoCore-GD32 @ `c6c5e463129cebb578c244c170c2df7422a91a95` — libraries/FlashStorage/src/FlashStorage.h, FlashAsEEPROM.h, README.md; libraries/EEPROM/src/EEPROM.h; platform.txt; boards.txt; variants/GD32F330F4_GENERIC/ldscript.ld
  - Issues: [#140](https://github.com/CommunityGD32Cores/ArduinoCore-GD32/issues/140), [#134](https://github.com/CommunityGD32Cores/ArduinoCore-GD32/pull/134)
- PaulStoffregen/cores @ `b296e5949c4b2f236127d9145c04b580a607064e` — teensy3/eeprom.c, teensy3/avr/eeprom.h, teensy3/mkl26z64.ld
- dbuezas/lgt8fx @ `3f38ee69959b1d0294b07dc046db2d606f0a116b` — lgt8f/libraries/E2PROM/EEPROM.h, EEPROM.cpp, lgt8f/boards.txt, lgt8f/platform.txt
  - Issues: [#254](https://github.com/dbuezas/lgt8fx/issues/254), [#261](https://github.com/dbuezas/lgt8fx/pull/261)
- MCUdude/MiniCore @ `d62c83555b66b7650e635e52415b6a436a342bb5` — avr/libraries/EEPROM/src/EEPROM.h, avr/libraries/Flash, avr/platform.txt, avr/boards.txt
- SpenceKonde/megaTinyCore @ `0f883addb31b71e8febaf493369ae259065a1e9f` — megaavr/libraries/EEPROM (README.md, src/EEPROM.h), USERSIG/README.md, Optiboot_flasher/README.md and examples, megaavr/platform.txt
- energia/msp430-lg-core @ `d7e0714610eb55e18dcaae9dec53af31002c42fe` — libraries/MspFlash/MspFlash.h, MspFlash.cpp, platform.txt.template
- mattairtech/ArduinoCore-samd @ `8b7398d83fe5baa680cf8e7e1262383b7486c632` — libraries/ (no storage library), README.md
- cmaglie/FlashStorage @ `634e7fd0c84120260d52ab75dd7687980941c3d9` — src/FlashStorage.h, src/FlashAsEEPROM.h/.cpp, readme.md

Not covered: Teensy Loader / HalfKay upload behaviour (closed source); exact code sizes (no toolchain available; none of the projects states a byte count for its storage code).
