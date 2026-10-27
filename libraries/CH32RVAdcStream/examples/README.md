# CH32RVAdcStream examples

[日本語](README.ja.md)

The ADC never stops; the sketch takes the buffers the DMA has filled.

## Boards and connections

Families whose ADC1 DMA request is DMA1 channel 1 ([library README](../README.md)). The log goes to the hardware UART `Serial` at 115200 baud 8N1. Change the pins in each `.ino`.

## The examples

| Example | What it teaches |
|---|---|
| [AdcStreamStats](AdcStreamStats/README.md) | Capture one pin continuously and take each full half to summarise it. |
