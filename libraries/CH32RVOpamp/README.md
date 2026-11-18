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

| Series | OPAs | Pads |
|---|---|---|
| V003 | 1 | + PA2 / PD7, - PA1 / PD0, output PD4 |
| V20x / V30x | 2 / 4 | two each of +, - and output per op-amp |
| V4x7 | 1 | + PB12 / PA6, - PB13 / PA5, output PB1 / PA7; PGA |

V00x, V205, L103, M030, X035 and H41x have op-amps too and their pads are in CH32RV_OPA_PADS, but this library
does not set their selection registers yet: `begin()` is false there.

## Test

selfapi::test_opamp: + and - driven as GPIOs, the open-loop output read with analogRead (seen on V203 and V407;
V407 also with the PGA at 4).
