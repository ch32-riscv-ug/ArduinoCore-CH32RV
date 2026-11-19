# CH32RVPioc

[日本語](README.ja.md)

The PIOC: WCH's RISC8B I/O processor on X035 / X033, V205 and H41x - an 8-bit core with its own 4 KiB program SRAM
and two pads (IO0, IO1) it drives cycle by cycle. Its program is written in the sketch's (or a library's) `.S` with
the core's macros (`<ch32rv_pioc.inc>`) and assembled by the sketch's own clang; no other assembler is involved.

On X035 / X033 and V205 the PIOC's program SRAM is 4 KiB of the ordinary RAM (X035: the top, the stack then sits below
it; V205: the bottom, `.data` and the rest above it). The linker sets it aside only for a sketch that uses the PIOC;
one that does not keeps all its RAM. X035's pads are chosen by the PCFR1 remap (IO0 / IO1 on PC18 / PC19, or IO0 on
PC7); PC18 / PC19 are SWD's too. The bench test passes on H417, X035 and V205 (PA0, PC7, PB10; 2026-11-19).

## Writing the program

```c
// pulses.S
#include <ch32rv_pioc.inc>

PIOC_BEGIN pulses
        DW     0x0000                   // word 0: reserved, never executed
START:  BC     SFR_PORT_IO, SB_PORT_OUT0
        MOVA1F 0x01                     // IO0 an output
WAIT:   WAITB  WB_DATA_MW_SR_1          // the host wrote cmd.in
        MOV    SFR_CTRL_WR, A
        ...
PIOC_END pulses
```

- The mnemonics, their aliases, the `f,d` operands (d: A / 0 to A, F / 1 / nothing to the register) and the SFR and
  bit names are WCH's (the RISC8B instruction set manual, the EVT's `PIOC_INC.ASM`): a vendor ASM moves over line by
  line.
- What differs: comments are `//` or `/* */` (the file goes through the C preprocessor; `;` separates statements and
  `#` starts a directive), constants are `.equ NAME, value` (or `#define`), and the program sits between
  `PIOC_BEGIN name` and `PIOC_END name`. `ORG`, `DW` and `DB` (RETL) are macros; `ORG` only moves forward and fills
  with NOP.
- Checked as it assembles: every immediate, register and bit fits; JMP / CALL targets are inside the 2048 words; a
  conditional jump (JNZ / JZ / JNC / JC) keeps the target's bit 10 equal to the next word's (the core replaces the
  low 10 bits only); CMPZ keeps bits 10:8 (the low 8 only); the image is at most 4096 bytes. Forward targets
  included, all at `PIOC_END`, with an `.error` naming the branch.
- Only PIOC code belongs in such a file: the macros (NOP, AND, RET, CALL ...) take the place of the RISC-V
  instructions of the same names.
- The output was checked by moving the EVT's 15 programs (5 each for X035, V205 and H417) to `.S` and matching the
  vendor's `.BIN` byte for byte (2026-11-19). The comparison ran outside this repository; no vendor binary is kept here.

## From the sketch

```cpp
#include <CH32RVPioc.h>

PIOC_IMAGE(pulses);                       // the .S's PIOC_BEGIN pulses
CH32RVPioc pioc;

void setup() {
    pioc.begin(PIOC_IMAGE_OF(pulses), PA0);  // IO0 on PA0, IO1 unused
}

void loop() {
    pioc.command(3);                      // cmd.in (SFR_CTRL_WR)
    int status = pioc.waitDone(100);      // cmd.out (SFR_CTRL_RD), -1 on a time-out
}
```

| API | What it does |
|---|---|
| `begin(image, io0, io1)` | Stops the PIOC, copies the image into its SRAM, hands the pads to it and runs it from word 1. -1: that pad unused. False on a part without a PIOC, an empty or over-4-KiB image, or a pad the PIOC does not reach |
| `end()` | Stops it (held in reset, its clock off) and gives the pads back |
| `command(v)` | Writes cmd.in; the program waits with `WAITB WB_DATA_MW_SR_1` and reads it with `MOV SFR_CTRL_WR, A` |
| `done()` | The program set `SB_INT_REQ` |
| `waitDone(ms)` | Until `done()`, then cmd.out (reading it clears the request); -1 on a time-out |
| `result()` | cmd.out now |
| `writeData(i, p, n)` / `readData(i, p, n)` / `write(i, v)` / `read(i)` | The shared registers d0 .. d31 (`SFR_DATA_REG0` + i) |

The pads the PIOC reaches are the variant's `CH32RV_PIOC_PADS` (H417: IO0 on PA0, PB8, PC1, PE3, PF12; IO1 on PB9,
PC0, PC2, PE4, PF13, PF14; PB8 / PB9 are SWD's too).

## Examples

- [PiocPulses](examples/PiocPulses/README.md): n pulses on IO0 for each command n.
