# FlashSettingsTwoBanks

[日本語](README.ja.md)

Keep a few settings in the internal flash and read them back after a reset. Two pages are written in turn, so a power cut while saving leaves the settings saved before. No extra component is needed.

**An example for advanced use.** For data that must last, an SD card or an I2C EEPROM is the recommended place. The limits of the internal flash are listed in [storing in flash](../../../../docs/flash-store.ja.md).

## Operation and API

The operation is the same as [FlashSettings](../FlashSettings/README.md): send a number to save it, reset to read it back.

- `CH32RV_FLASH_STORE(store, 2 * CH32RV_FLASH_ERASE_SIZE)` reserves two erase units.
- Each page starts with a CRC-16 and a generation. Reading uses the newer of the pages whose CRC matches.
- Saving writes the older page with `ch32rv_flash_rewrite_page()`. If power is cut while it is written, the newer page is still there.
- The version (`kVersion`) and size check, and skipping unchanged contents, are as in FlashSettings.
- The two pages are erased in turn, which halves the erases of each.

## Cost

The load and save part alone adds about 670 B on V003 and 730 B on X035. RAM does not grow. It reserves twice the flash of FlashSettings (8 KB on series that erase by 4 KB, 16 KB on H415/H417). Per-series values are in [storing in flash](../../../../docs/flash-store.ja.md).

## Before running

Select your board and exact Part Number. UART logging uses hardware `Serial` at 115200 baud, 8N1: connect adapter RX to board TX, adapter TX to board RX, and common GND.

Uploading again keeps what was saved (Tools > Erase Flash on its default, Keep Store); All Flash erases it. The generated `sketch.yaml` supplies build profiles. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
