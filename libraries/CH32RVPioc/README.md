# CH32RVPioc

[日本語](README.ja.md)

The PIOC: WCH's RISC8B I/O processor on X035 / X033, V205 and H41x - an 8-bit core with its own 4 KiB program SRAM
and two pads (IO0, IO1) it drives cycle by cycle. Its program is written in the sketch's (or a library's) `.S` with
the core's macros (`<ch32rv_pioc.inc>`) and assembled by the sketch's own clang; no other assembler is involved.

On X035 / X033 and V205 the PIOC's program SRAM is 4 KiB of the ordinary RAM (X035: the top, the stack then sits below
it; V205: the bottom, `.data` and the rest above it). The linker sets it aside only for a sketch that uses the PIOC;
one that does not keeps all its RAM. X035's pads are chosen by the PCFR1 remap (IO0 / IO1 on PC18 / PC19, or IO0 on
PC7); PC18 / PC19 are SWD's too. The bench test passes on H417, X035 and V205 (PA0, PC7, PB10; 2026-11-19).

## What it is for, and its limits

The PIOC is "a small 8-bit core with two pads that keeps time to the clock". It takes one fixed job - usually a pad
waveform - off the main core. It does not run anything you like.

**Good for**
- One- or two-wire protocols the hardware has no peripheral for, without stopping the main core's interrupts:
  many WS2812 LEDs, 1-Wire (DS18B20), NEC IR send / receive, home-made single-wire links, Manchester coding.
- Measuring or making pulse widths and gaps that the main core's interrupts (USB, millis()) would jitter.
- Watching the pads and telling the main core only when something happens (`onDone()`'s interrupt).

**Not for**
- What a hardware peripheral does (USART, I2C, SPI, timer PWM / capture): faster, and any routed pad.
- Three or more wires (4-wire SPI, parallel buses): the PIOC has IO0 and IO1, on a few pads only (X035: IO0 on PC18
  or PC7, IO1 on PC19; PC18 / PC19 are SWD's too).
- Streaming a lot of data: no DMA, no FIFO; the main core and the PIOC share 32 bytes of registers and a command
  byte. Long data goes into the PIOC's 4 KiB SRAM, which also holds the program.
- Heavy computation: 8 bits, no multiply, a 6-deep stack.
- Several jobs at once: one program runs; another job means loading another image.

**Timing**: the PIOC runs at HCLK (measured: H417 about 100 MHz, X035 48 MHz, V205 160 MHz). Instructions take 1 to 3
cycles (a taken branch, CALL, RET: 2; RDCODE: 3), so on X035 an instruction is about 21 ns and a loop that toggles a pad
takes a few cycles at least. Waits are made of instructions, so another clock means another speed (count again per
family).

**Against the Raspberry Pi Pico's PIO**: PIO has four small one-instruction-per-clock state machines per block, any
GPIOs several at a time, and FIFOs and DMA for fast streams - but 32 instructions per block and next to no
computation or decisions. The PIOC is the other way round: one of it, two pads, no FIFO or DMA, but general instructions -
compares, add / subtract, conditional jumps, subroutines (6 deep) - so decisions can be written; its program lives
in 4 KiB of SRAM (at most 2048 words, fewer when long data shares it). PIO programs do not carry over, and "fast, wide,
streaming" PIO-style jobs do not fit. Its place is "a slower protocol with decisions in it, on one or two wires,
done to the clock instead of by the main core".

**Against the ESP32's ULP**: both are a small core that runs apart from the main one and reports when done. But the
ULP is mainly a watcher while the main core is in deep sleep (low power). The PIOC runs on HCLK, so it stops in any
sleep that stops the clock (measured on X035, V205 and H417, 2026-11-19):

| The main core | The PIOC |
|---|---|
| A plain WFI (Sleep; HCLK runs) | Keeps running; its request (`onDone()`) wakes the main core |
| The stop part of `CH32RV.lightSleep()` | Stops, and goes on where it was after the wake (a pulse under way is stretched); its request does not wake the main core |
| `CH32RV.deepSleep()` (standby, a restart) | Stops, and the program is lost (`begin()` again after the wake) |

On the RTC families (V205, H417 ...) `lightSleep()` spends a sleep under a second in a plain WFI, so the PIOC runs
through it; X035 sleeps in the AWU's stop, so it stops even for a short one. No API makes the PIOC's request a reason
for `lightSleep()` to end yet. So it is not a low-power watcher: it takes timing-critical pad work off the main core
while that core runs or waits in a WFI.

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
| `waitDone(ms)` | Until `done()`, then cmd.out with the request taken down; -1 on a time-out |
| `result()` | cmd.out now, the request taken down (`SB_INT_REQ` falls when the host writes cmd.out, not when it reads it; measured on X035 2026-11-19) |
| `onDone(cb)` | The requests by interrupt: it reads cmd.out, takes the request down and calls `cb(cmd.out)` (in the interrupt: keep it short). `nullptr` goes back to `waitDone()` |
| `writeData(i, p, n)` / `readData(i, p, n)` / `write(i, v)` / `read(i)` | The shared registers d0 .. d31 (`SFR_DATA_REG0` + i) |
| `loadSram(offset, p, n)` | Data into the program SRAM past the image (from byte `offset`); the program reads it with `RDCODE`, a word (`offset / 2`) at a time. The SRAM is the PIOC's alone while it runs, so this holds it in reset, copies and starts it again from its beginning (d0 .. d31 are cleared too: write them after). False when it would overlap the image or pass 4 KiB |
| `clockHz()` | The PIOC's clock (HCLK; slower than the main core on H41x). A program's timing is counted in its cycles |

The pads the PIOC reaches are the variant's `CH32RV_PIOC_PADS` (H417: IO0 on PA0, PB8, PC1, PE3, PF12; IO1 on PB9,
PC0, PC2, PE4, PF13, PF14; PB8 / PB9 are SWD's too).

## Examples

- [PiocPulses](examples/PiocPulses/README.md): n pulses on IO0 for each command n.
- [PiocWs2812](examples/PiocWs2812/README.md): WS2812 (NeoPixel) LEDs by the PIOC; the colours go into the SRAM by `loadSram()` and the waits are worked out from `clockHz()`. WireSkein's `rgb_led` passes on H417, X035 and V205 (2026-11-19).
