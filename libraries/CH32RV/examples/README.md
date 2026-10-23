# Basic examples

[日本語](README.ja.md)

Small examples for GPIO, UART, ADC, PWM and interrupts. Each folder's README covers connections, operation, API roles and adaptation notes.

## Preparation

In Arduino IDE, choose File → Examples → CH32RV and select the board and exact Part Number. Generic boards do not define PCB wiring; adapt pin constants at the top of each sketch to your schematic. Start with PinCapabilities to inspect aliases such as `A1` or `RX`.

Most examples log through hardware `Serial`. Connect adapter RX to board TX, TX to RX, and common GND. Match I/O voltage and `Serial.begin()` settings. A USB connector alone does not mean that UART output can be read through it.

With the core installed, compile from the repository root:

```sh
arduino-cli compile --fqbn ch32-riscv-ug:ch32rv:CH32X035:pnum=CH32X035F8U6 \
  libraries/CH32RV/examples/Timing
```

Blink needs the actual board LED assigned to `LED_BUILTIN`. Use a product-board definition when provided. For a Generic board, define it in the sketch or pass the actual pin through a build option.

## Choose an example

| Example | What it teaches |
|---|---|
| [Blink](Blink/README.md) | Toggle a GPIO to confirm sketch execution with an LED. |
| [SerialEcho](SerialEcho/README.md) | Receive a line over hardware UART and echo it with surrounding whitespace removed. |
| [AnalogRead](AnalogRead/README.md) | Observe raw analog input readings on UART. |
| [AnalogResolution](AnalogResolution/README.md) | Change the numeric scales used by ADC and PWM APIs. |
| [Fade](Fade/README.md) | Vary PWM duty to fade an LED up and down. |
| [PinInterrupt](PinInterrupt/README.md) | Count GPIO edges in an interrupt and defer printing to `loop()`. |
| [CriticalSection](CriticalSection/README.md) | Demonstrate copying interrupt-shared values inside a short interrupt-disabled section. |
| [PinCapabilities](PinCapabilities/README.md) | Inspect variant-defined pins and their ADC mappings. |
| [PrintFormatting](PrintFormatting/README.md) | Compare integer, floating-point and string formatting with raw byte output. |
| [PulseIn](PulseIn/README.md) | Generate a trigger and measure the duration of a High input pulse. |
| [RandomNumbers](RandomNumbers/README.md) | Compare reproducible pseudorandom sequences with a changing seed. |
| [ShiftOut](ShiftOut/README.md) | Send bytes to a shift register and activate its outputs in sequence. |
| [Timing](Timing/README.md) | Demonstrate `millis()`, `micros()` and elapsed-time subtraction across wraparound. |
| [ToneMelody](ToneMelody/README.md) | Play a scale on a passive buzzer by specifying frequency and duration. |
| [TimerResource](TimerResource/README.md) | An advanced timer-ownership example for library authors: acquire a timer, set its base and attach an update ISR. |
| [BoardWeActCH32X035](BoardWeActCH32X035/README.md) | A starting point combining the peripherals exposed by the WeAct CH32X035 Core Board, with explicit pin names. |
| [BoardUIAPduinoProMicro](BoardUIAPduinoProMicro/README.md) | Initialize UART, I2C, SPI, ADC and PWM using the UIAPduino Pro Micro board pin assignments. |

Verify examples separately before combining them. Delays and extensive demonstration logging are not intended as the final structure for many concurrent features. Continue with the [sketch composition guide (Japanese)](../../../docs/sketch-guide.ja.md) for time management, ISR data transfer and callback handling.

See the [example guide](../../../docs/examples.md) for all libraries and [build rules (Japanese)](../../../docs/examples-build-rules.ja.md) for profiles.
