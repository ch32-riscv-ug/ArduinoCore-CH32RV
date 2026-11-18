# CH32RVComparator

[日本語](README.ja.md)

The chip's built-in comparators (CMP), their inputs and output chosen by pin.

```cpp
#include <CH32RVComparator.h>

CH32RVComparator cmp(1);                // CMP1

void onEdge() { /* the output changed */ }

void setup() {
  cmp.setHysteresis(25);                // mV: the nearest level the chip has, 0 off
  cmp.begin(PA2, PB1, PB12);            // +, -, output: that comparator's pads (the variant's CH32RV_CMP_PADS)
  attachInterrupt(digitalPinToInterrupt(PB12), onEdge, CHANGE);
}

void loop() {
  int above = cmp.read();               // 1: + above -
}
```

- `read()` takes the comparator's status bit where there is one (H41x), else the output pad's level: -1 with
  neither (no output pad, `outPin` -1).
- The output's edges come from `attachInterrupt()` on the output pad.
- Input pads stay as the sketch set them (an analog input, or a GPIO driving them); the output pad becomes the
  comparator's and goes back to an input at `end()`.
- Inner inputs: `CH32RVComparator::OPA_OUTPUT` as + (V205: the op-amp of the same number; H41x: OPA1),
  `CH32RVComparator::REFERENCE` as - (H41x: 1/4, 2/4 or 3/4 of the supply, `setReference(quarters)`, 2/4 by
  default).

## Families

| Series | CMPs | Pads (+ / - / output) | Hysteresis |
|---|---|---|---|
| V007 / M007 | 1 | PC5, PB3, PD2 / PC2, PD5, PA6 / PC0 | 24 mV |
| V205 | 2 | CMP1: PA2, PB2 / PB1, PB3 / PB12; CMP2: PB8, PA8 / PA9, PB11 / PA12 | 12.5, 25, 50 mV |
| L103 / M103 | 3 | CMP1: PA2, PB2 / PB1, PB3 / PB0, PB10; CMP2: PB8, PB6 / PB3, PB7 / PB5, PB11; CMP3: PB5, PB10 / PB3, PB11 / PB4, PB12 | 15 mV |
| X035 / X033 | 3 | CMP1: PC19, PA0 / PC3, PA23 / PA1; CMP2: PA12, PA11 / PA22, PC3 / PB2; CMP3: PA13, PA14 / PA2, PC3 / PB3 | on (no value in the manual) |
| H41x | 1 | PB0, PB2 / PB1, PC4, PA5 (DAC2) / PA6, PA8, PB12, PC5, PE6, PE12, PE15 | 10, 20, 30 mV |

The pads and their selection values come from each family's reference manual (tools/generate/opa/cmp_select.csv).
M030's comparators (the motor stage's) are not covered; V205's 4-bit DAC as -, and the outputs to timers, brake
and wake-up are not there yet.

## Test

selfapi::test_comparator: the first + and - off the board's keep-off list driven as GPIOs, `read()` and the output
pad's interrupt (two edges) checked; on H41x, + against the inner reference (half the supply) too. Seen on V205,
X035 and H417.
