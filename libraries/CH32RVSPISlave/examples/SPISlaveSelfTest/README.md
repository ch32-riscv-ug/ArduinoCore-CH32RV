# SPISlaveSelfTest

[日本語](README.ja.md)

Checks [CH32RVSPISlave](../../README.md) with no master and no wiring. The sketch opens the slave on the SPI1 pins, then drives SCK, MOSI and CS itself as GPIO outputs: the SPI reads the pads, so the chip is its own master. MISO is read back with `digitalRead()`.

## Wiring

None. **Nothing may be connected to the SPI1 pins or the CS pin**, because the sketch drives them:

| | SCK | MISO | MOSI | CS |
|---|---|---|---|---|
| CH32V00x / M007 | PC5 | PC7 | PC6 | PC4 |
| CH32H41x | PF5 | PF3 | PD7 | PF4 |
| the others | PA5 | PA6 | PA7 | PA4 |

## What it checks

For SPI modes 0 to 3, and mode 0 LSB first:

1. a queued transfer both ways;
2. a chip select with nothing queued (the fill byte, `missed()`);
3. the master going on past the buffer (the fill byte after `tx`; `length` counts every byte);
4. two queued transfers in turn, and a CS pulse with no clock that uses nothing up;
5. the result through `onTransfer()` and `deliverEvents()`.

## Output

Serial at 115200: the steps, then `SPISLAVE PASS` or `SPISLAVE FAIL`, once a second. A failed step prints the bytes it got and wanted. For a probe without a console, the global `g_result` is 1 (pass) or 2 (fail) and `g_failed_step` names the first failed step.

[SPISlaveSelfTest.ino](SPISlaveSelfTest.ino)
