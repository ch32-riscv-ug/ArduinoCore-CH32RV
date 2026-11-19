# CH32RVOpamp

[日本語](README.ja.md)

The chip's built-in op-amps (OPA), their inputs and output chosen by pin.

```cpp
#include <CH32RVOpamp.h>

CH32RVOpamp opa(1);                     // OPA1

void setup() {
  opa.begin(PB15, PB11, PA3);           // +, -, output: that op-amp's pads (the variant's CH32RV_OPA_PADS)
}
```

- With no feedback wired it compares + with - (open loop): the output swings to the rail of whichever is higher.
  Wire the output to - for a voltage follower, or through resistors for a gain.
- `beginPga(plus, gain, out)` amplifies with the internal feedback (V4x7: 4, 8, 16, 32; no - pad). False where
  there is no PGA or for a gain it does not have.
- Input pads stay as the sketch set them (an analog input, or a GPIO driving them); the output pad becomes the
  op-amp's. `end()` stops it.
- Without an output pin, `begin()` takes the op-amp's first output pad.

## Families

| Series | OPAs | Pads (+ / - / output) | PGA |
|---|---|---|---|
| V003 | 1 | PA2, PD7 / PA1, PD0 / PD4 | none |
| V00x (V005 .. V007, M007) | 1 | PA2, PD7, PD3, PD1 / PA1, PD0 / PD4, PA5 | x4 .. x32 |
| V20x / V30x | 2 / 4 | two each per op-amp (CH0P, CH1P, ...) | none |
| V205 | 2 | OPA1: PB5, PB0, PB12 / PB11, PA6 / PB1, PA3; OPA2: PB14, PA7 / PB10, PA5 / PA4, PA2 | OPA1 x8 .. x64, OPA2 x4 .. x32 |
| V4x7 | 1 | PB12, PA6 / PB13, PA5 / PB1, PA7 | x4 .. x32 |
| L103 / M103 | 1 | PB15, PB0, PB14, PA7, PA0, PA6 / PB11, PA6, PB10, PA5, PA1, PA7 / PA3, PB1, PA2, PA4, PB0 | x8 .. x64 |
| X035 / X033 | 2 | OPA1: PB0, PB8, PB4 / PA6, PB6 / PA3, PB5; OPA2: PA7, PB3, PB7 / PA5, PB1 / PA4, PA2 | x4 .. x32 |
| H41x | 3 | OPA1: PB0, PA6 / PB1, PA7 / PC4, PA5; OPA2: PE9, PF11 / PE8, PF12 / PE7, PB1; OPA3: PC2, PA2 / PC3, PA3 / PA0, PA4 | x8 .. x64 |

The pads and their selection values come from each family's reference manual (tools/generate/opa/opa_select.csv;
V20x / V30x take device-data's numbering). Pads a small package lacks are not in its variant's CH32RV_OPA_PADS.
M030's op-amps (the motor stage's current sense) are not covered.

## Test

selfapi::test_opamp: on the first +, - and output pads that are not among the board's pads to keep off, + and -
driven as GPIOs and the open-loop output read with analogRead; where there is a PGA, its lowest gain the same way.
Seen on V006, V203, V205, V407, X035 and H417 (not V003, whose test board keeps every + pad off, nor L103).
