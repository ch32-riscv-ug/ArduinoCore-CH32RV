# Crc32

[日本語](README.ja.md)

Check data with `CH32RV.crc32()`, the CRC block's CRC-32, computed in hardware where the chip has the block.

## Hardware and connections

Connect a UART monitor.

## Steps and expected results

At startup the check string `123456789` gives `0x376E6E7` both in one call and in two pieces. Every two seconds a packet is made, one bit is flipped, and `packet damaged: CRC mismatch` is printed.

## API and processing flow

`CH32RV.crc32(data, len)` returns the CRC of `len` bytes; `CH32RV.crc32(data, len, crc)` continues from an earlier result, so data can be checked as it arrives. The CRC is CRC-32/MPEG-2: polynomial 0x04C11DB7, starting at 0xFFFFFFFF, no reflection and no final XOR - what the CRC block computes (V103, V20x, V205, V30x, V4x7, L103 / M103, X305 / X315, H41x). Other chips compute the same in software. The C function is `ch32rv_crc32()` in `ch32rv_crc.h`.

## Adapting the example

This is not zlib's or Ethernet's CRC-32 (the same polynomial, reflected, with a final XOR: `0xCBF43926` for the check string); a protocol that names that one needs a software CRC of its own. A CRC detects accidental damage; it does not protect against deliberate changes.

## Related documentation

- [Sketch](Crc32.ino)
- [Basic example setup and index](../README.md)
- [Example guide](../../../../docs/examples.md)
- [Sketch composition guide (Japanese)](../../../../docs/sketch-guide.ja.md)
