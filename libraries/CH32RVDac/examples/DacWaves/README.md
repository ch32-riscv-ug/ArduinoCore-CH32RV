# DacWaves

[日本語](README.ja.md)

DAC1 puts out a 1 kHz sine from 64 samples (DMA), the converter's own triangle, then noise, two seconds each.

## What you need and wiring

A chip with a DAC (V30x, V4x7, H41x). DAC1's pad (`CH32RV_DAC1_PIN`, PA4) to an oscilloscope.

## Operation and expected result

`Serial` prints the timer's actual rate at each change, such as `sine: steps/s 64000`.
PA4 carries a 1 kHz sine of about 0.1 .. 3.2 V, a triangle of 1024 .. 2047 (about 0.8 .. 1.65 V) at about 98 Hz, then noise 255 wide above 2048.

## API and flow

`play()` sends the sample array round and round to the DHR by DMA; `triangle()` and `noise()` use the converter's wave
generator. Each steps on a timer's TRGO, so the CPU does nothing while they run. Calling another wave stops the one before.
