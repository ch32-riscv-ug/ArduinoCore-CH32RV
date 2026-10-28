# FlashSettings

[日本語](README.ja.md)

Keep a few settings in the internal flash and read them back after a reset. The smallest form: one page, rewritten in place. No extra component is needed.

**An example for advanced use.** For data that must last, an SD card or an I2C EEPROM is the recommended place. The limits of the internal flash are listed in [storing in flash](../../../../docs/flash-store.ja.md).

## Operation and API

Send a number from the terminal: the sketch sets `interval` to it and saves. Reset the board and it prints `loaded interval …` with the value read back. With nothing saved, or saved in another layout, it prints `defaults` and uses the defaults.

- `CH32RV_FLASH_STORE(store, CH32RV_FLASH_ERASE_SIZE)` reserves one erase unit at the end of the flash. Nothing is reserved unless the sketch uses it.
- Read `store` as ordinary memory.
- Write with `ch32rv_flash_rewrite_page()`: it erases the unit and programs one page (`CH32RV_FLASH_WRITE_SIZE`).
- The page starts with a version (`kVersion`) and the size, checked when reading. Change `kVersion` when `Settings` changes. An erased word reads differently by series (0xFF or 0xE339), so blank bytes are not used to tell a saved page.
- Nothing is written when the contents did not change.

A power cut while saving loses the settings (the next start uses the defaults). To keep the previous settings, use [FlashSettingsTwoBanks](../FlashSettingsTwoBanks/README.md).

## Cost

The load and save part alone adds about 440 B on V003 and 500 B on X035. RAM does not grow (one page of stack while saving). Per-series values are in [storing in flash](../../../../docs/flash-store.ja.md).

## Before running

Select your board and exact Part Number. UART logging uses hardware `Serial` at 115200 baud, 8N1: connect adapter RX to board TX, adapter TX to board RX, and common GND.

Uploading again may erase what was saved (depends on the route and the tool version). The generated `sketch.yaml` supplies build profiles. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
