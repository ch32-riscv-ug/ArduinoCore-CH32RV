# I2S

[日本語](README.ja.md)

Audio in or out on an SPI in I2S mode (I2S2, I2S3) of CH32V30x, CH32V4x7 and CH32H41x, on CH32H417's SAI (standard and TDM), and from PDM microphones on CH32H41x's DFSDM. The API has the shape of arduino-esp32's ESP_I2S (the header is `I2S.h`, the class `I2SClass`).

```cpp
#include <I2S.h>

I2SClass i2s;

void setup() {
  i2s.setPins(PB13, PB12, PB15);            // BCLK, WS, DOUT (an input: setPins(bclk, ws, -1, din))
  i2s.begin(I2S_MODE_STD, 48000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
}

void loop() {
  int16_t frame[2] = {left, right};
  i2s.write((const uint8_t *)frame, sizeof(frame));   // waits while the buffer is full
}
```

| Function | Meaning |
|---|---|
| `setPins(bclk, ws, dout, din = -1, mclk = -1)` | The pins. dout makes an output, din an input (one of them). mclk: a 256 fs master clock |
| `setFormat(format)` | `I2S_FORMAT_PHILIPS` (default), `_MSB` (left-justified), `_LSB` (right-justified), `_PCM_SHORT`, `_PCM_LONG`. Before begin() |
| `setPinsPdmRx(clk, din0)` | PDM microphones (`I2S_MODE_PDM_RX`): CH32H41x's DFSDM - clk a CKOUT pad, din0 a DATIN pad (the variant's `CH32RV_DFSDM_PADS`) |
| `setInverted(bclk, ws)` | BCLK's polarity inverted; WS swaps the slots |
| `setBufferSize(bytes)` | Each half of the DMA buffer (default 1024 bytes). Before begin() |
| `begin(I2S_MODE_STD, I2S_MODE_TDM (SAI) or I2S_MODE_PDM_RX, rate, bits, slots, slot_mask = -1)` | bits 16, 24 or 32; slots `I2S_SLOT_MODE_MONO` / `_STEREO` |
| `write(buf, n)` / `availableForWrite()` | Output |
| `readBytes(buf, n)` / `read()` / `available()` | Input. readBytes() waits up to `setTimeout()` (1 s by default) |
| `flush()` | Waits until what was written has played |
| `sampleRate()` | The rate the divider actually gives |
| `underruns()` / `overruns()` | Halves of the output buffer not filled in time / input lost by reading late |
| `lastError()` | `I2S_ERROR_ARG` (rate, width), `_PINS`, `_DMA`, `_MEMORY`, `_STATE` |
| `end()` | Stops, the pins back to inputs |

- Samples in memory as ESP_I2S has them: little-endian, left then right in stereo; 16-bit as `int16_t`, 24-bit as three bytes, 32-bit as `int32_t`. A mono output puts each sample on the slots of slot_mask (both by default); a mono input reads the left slot (the right with `I2S_STD_SLOT_RIGHT`).
- The controller (master) side only, standard (two-slot) mode only: the SPI's I2S has no TDM or PDM (H417's SAI is another peripheral). PDM microphones go through CH32H41x's DFSDM instead (`I2S_MODE_PDM_RX`, below). The SPI's I2S is half duplex, so an instance is an output or an input; for both at once, two instances on I2S2 and I2S3.
- The pins pick I2S2 or I2S3. The variant lists them in `CH32RV_I2S2_PADS` / `CH32RV_I2S3_PADS`. H41x chooses the function per pad, so any combination works there; on V30x / V4x7 they have to share one AFIO route (the SPI's field).
- The clock is SYSCLK divided by 2 x I2SDIV + ODD (I2SDIV 2..255): the rate is a little off the one asked for, and `sampleRate()` tells. A rate the divider cannot make (16-bit below SYSCLK / 16352: 8.8 kHz at 144 MHz, 24.5 kHz at H41x's 400 MHz) fails begin().
- The output is a buffer whose two halves the DMA plays in turn. A half not filled in time plays as silence, so stopping writing stops the sound. The input is a ring the DMA fills; what is not read in time is overwritten (`overruns()`).
- The DMA's half / full interrupts are this library's (the interrupts of SPI2's and SPI3's DMA channels).
- An SPI cannot be used by the SPI library and this one at the same time.

### SAI (CH32H417)

```cpp
i2s.setPins(PF8, PF9, PA13);                // SCK_B, FS_B, SD_B: block B of the SAI (FS -1: not brought out)
i2s.begin(I2S_MODE_TDM, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, 0x0F);   // slots 0 .. 3
int16_t frame[4] = {a, b, c, d};
i2s.write((const uint8_t *)frame, sizeof(frame));
```

- When the pins are one SAI block's (the variant's `CH32RV_SAI_PADS`: SCK, FS, SD and MCLK of block A or B), the SAI takes them; standard mode on pins that are an SPI's I2S as well picks the SPI. Output and input, standard mode (the formats above) and `I2S_MODE_TDM`.
- TDM: `slot_mask` is the slots used (ESP_I2S's `int8_t`: slots 0 .. 6; -1: slot 0, or 0 and 1 in stereo); the frame is 2, 4 or 8 slots (the slots past the highest used are off), 16 bits each for 16-bit samples and 32 for 24 / 32; one sample per slot used in memory. Philips: FS low for the first half of the frame, one bit early; the PCM formats: a 1- or 13-bit pulse.
- The clock: SAI_CK = SYSCLK / 6 and MCLK = SAI_CK / MCKDIV (1 .. 63) = 256 fs, so the rates are coarse: 32552 Hz for 32 kHz, 16276 for 16 kHz, 43403 for 44.1 kHz and 52083 for 48 kHz at 400 MHz (`sampleRate()` tells). FS may be left out (-1) where nothing needs it.
- No DMA channel: the SAI's FIFO interrupt moves the samples (one interrupt for both blocks). A sketch that uses I2S on CH32H417 carries the SAI's code (about 3 KB).

### PDM microphones (CH32H41x)

```cpp
i2s.setPinsPdmRx(PE9, PD6);                 // CKOUT to the microphones' CLK, their DATA on DATIN1
i2s.begin(I2S_MODE_PDM_RX, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
int16_t frame[2];
i2s.readBytes((char *)frame, sizeof(frame));   // left, right
```

- The DFSDM (the library [CH32RVDfsdm](../CH32RVDfsdm/README.md)) rather than the SPI: the clock is 64 x rate (HCLK divided: 15943 Hz for 16 kHz at H417's 100 MHz HCLK; `sampleRate()` tells), a sinc3 filter / 64, scaled to the width's full scale.
- Stereo is two microphones on din0: the left sends on CLK's rising edge, the right on the falling (each microphone's L/R or SELECT pin). Mono reads the left, or the right with `I2S_STD_SLOT_RIGHT`.
- One line only (din1 .. din3: -1); setPinsPdmRx() is in its own file, so a sketch that never calls it links neither it nor CH32RVDfsdm. PDM output (`I2S_MODE_PDM_TX`) is not here.

## Examples

- [SineWave](examples/SineWave/README.md): a sine on an I2S DAC.

## Tests

spi::test_i2s: the output captured on three labelled lines and judged by WireSkein's i2s (16- and 32-bit Philips, 24-bit left- and right-justified, 16-bit mono: eight random frames in a row, the rate within 1 % of 32 kHz, the slot's BCLKs). spi::test_i2s_input: the probe's PWM on SD, the share of all-ones / all-zeros slots and the left slot's rises the DUT read compared with the PWM. Passing on H417 (2026-11-23). V30x / V4x7 built only (no bench lines make an I2S there). spi::test_sai: on SAI block B's SCK and SD (FS not on a bench line) - SCK a square at sampleRate() x the frame's bits (16-bit standard, TDM 4 x 16 and 8 x 32) judged by WireSkein, and as an input the probe's PWM read as test_i2s_input does (H417, 2026-11-23); the SD bit stream decoded once carried every sample in order. periph::test_i2s_pdm_rx: `I2S_MODE_PDM_RX` at 16 kHz, the probe's PWM making the stream: duty 1/4 and 3/4 read a mean of -1/2 and +1/2 of 32767 in mono and in both stereo slots, no overrun, CKOUT judged by WireSkein (H417, 2026-11-23).
