# InternalAnalogRead

[日本語](README.ja.md)

Read the ADC's internal channels: the internal reference, the supply voltage worked out from it, and the die temperature. No wiring beyond the UART. Compare with [AnalogRead](../AnalogRead/README.md), which reads a pin.

## Operation

Every two seconds one line appears, for example `Vref reading 381  VDD 3220 mV  die 22.2 C (typical constants, not calibrated)` on a CH32V307. Warm the chip with a finger: the temperature rises by a few degrees. A series without a temperature sensor (V00x, X033 / X035, X305 / X315, M030) prints `(no sensor)`; where the supply cannot be worked out, `(unknown)`.

## API and adaptation

- `analogReadVref()`: the internal reference as `analogRead()` would return it at the current resolution; 0 where the chip has none.
- `CH32RV.getVddMillivolts()`: the supply in mV, from the reference's typical voltage - within a few percent. 0 when unknown, including a reference reading that would mean more than 6 V (a CH32X035C8T6 on the bench reads its ADC_IN15 as almost 0).
- `analogReadTemp()`: degrees C from the datasheet's typical constants, NAN without a sensor. Each part is off by several degrees (a CH32V103 reads about 20 C high); use it for changes, not for absolute temperature, unless you calibrate it.

The sketch averages 16 readings, because one reading is noisy. Do not call these while CH32RVAdcStream runs: they use the same ADC.

## Before running

Select your board and exact Part Number. Output is hardware `Serial` at 115200 baud, 8N1: connect adapter RX to board TX, adapter TX to board RX, and common GND. The board's USB connector does not automatically carry this UART.

The generated `sketch.yaml` supplies build profiles. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
