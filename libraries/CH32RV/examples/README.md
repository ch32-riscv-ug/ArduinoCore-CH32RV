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
| [AnalogRead](AnalogRead/README.md) | Observe raw analog input readings and their voltage in mV on UART. |
| [AnalogContinuous](AnalogContinuous/README.md) | Average frames the ADC fills by itself, with arduino-esp32's continuous ADC API (`analogContinuous()`). |
| [InternalAnalogRead](InternalAnalogRead/README.md) | Read the internal reference, the supply voltage and the die temperature. |
| [AnalogResolution](AnalogResolution/README.md) | Change the numeric scales used by ADC and PWM APIs. |
| [Fade](Fade/README.md) | Vary PWM duty to fade an LED up and down. |
| [PinInterrupt](PinInterrupt/README.md) | Count GPIO edges in an interrupt and defer printing to `loop()`. |
| [CriticalSection](CriticalSection/README.md) | Demonstrate copying interrupt-shared values inside a short interrupt-disabled section. |
| [DualCoreV3F](DualCoreV3F/README.md) | CH32H417: run `setup1()` / `loop1()` on the V3F while the sketch runs on the V5F. |
| [DualCoreMessages](DualCoreMessages/README.md) | CH32H417: the V5F and the V3F talk through messages (IPC) and a hardware semaphore (HSEM). |
| [PinCapabilities](PinCapabilities/README.md) | Inspect variant-defined pins and their ADC mappings. |
| [PrintFormatting](PrintFormatting/README.md) | Compare integer, floating-point and string formatting with raw byte output. |
| [PulseIn](PulseIn/README.md) | Generate a trigger and measure the duration of a High input pulse. |
| [TimerAlarm](TimerAlarm/README.md) | Run a function every second with arduino-esp32's timer API (`timerBegin()`, `timerAlarm()`). |
| [RandomNumbers](RandomNumbers/README.md) | Compare reproducible pseudorandom sequences with one seeded from `ch32rv_random()`. |
| [Crc32](Crc32/README.md) | Check data with `CH32RV.crc32()`, in one call or in pieces. |
| [RgbLed](RgbLed/README.md) | Light WS2812 / SK6812 RGB LEDs on any pin with `rgbLedWrite()`. |
| [InterruptPriority](InterruptPriority/README.md) | Make a slow timer interrupt less urgent so `millis()` keeps counting through it. |
| [ShiftOut](ShiftOut/README.md) | Send bytes to a shift register and activate its outputs in sequence. |
| [Timing](Timing/README.md) | Demonstrate `millis()`, `micros()` and elapsed-time subtraction across wraparound. |
| [ToneMelody](ToneMelody/README.md) | Play a scale on a passive buzzer by specifying frequency and duration. |
| [TouchRead](TouchRead/README.md) | Read a TouchKey pad with `touchRead()` and report a touch (V00x, X035, V20x, V30x). |
| [TimerResource](TimerResource/README.md) | An advanced timer-ownership example for library authors: acquire a timer, set its base and attach an update ISR. |
| [SystemInfo](SystemInfo/README.md) | Print the build target, memory information and optional chip identity. |
| [ResetReason](ResetReason/README.md) | Compare the current reset reason with a command-triggered software reset. |
| [WatchdogRecovery](WatchdogRecovery/README.md) | Start the watchdog on command and observe a reset after feeding stops. |
| [WindowWatchdog](WindowWatchdog/README.md) | Start the window watchdog on command and see it reset on a late and on an early feed. |
| [RtcAlarm](RtcAlarm/README.md) | Keep the date in the RTC, ring an alarm, sleep until it, and count boots in a backup register. |
| [FlashSettings](FlashSettings/README.md) | Save settings in one page of the internal flash and read them back after a reset (advanced; an external SD card or EEPROM is recommended). |
| [FlashSettingsTwoBanks](FlashSettingsTwoBanks/README.md) | Save to two pages in turn so a power cut while saving keeps the previous settings (advanced). |
| [PwmFrequency](PwmFrequency/README.md) | Change PWM frequency while keeping the duty scale and value constant. |
| [SerialLineReceiver](SerialLineReceiver/README.md) | Receive bounded text lines incrementally, with overflow and timeout handling. |
| [SerialPacketReceiver](SerialPacketReceiver/README.md) | Parse length-prefixed binary packets with a checksum and timeout. |
| [SerialReceiveEvent](SerialReceiveEvent/README.md) | Take a whole message when the line goes quiet (`onReceive()`) and hear about receive errors. |
| [SerialFlowControl](SerialFlowControl/README.md) | Pace a sender with RTS and hold sending with CTS (`setHwFlowCtrlMode()`). |
| [SerialRS485](SerialRS485/README.md) | Query an RS485 bus; the core switches the transceiver's driver enable around each write. |
| [SerialHalfDuplex](SerialHalfDuplex/README.md) | Send and receive on one open-drain wire (`setPins(p, p)`). |
| [SerialDmaBuffers](SerialDmaBuffers/README.md) | Take a 1 Mbaud stream while `loop()` is busy: larger buffers moved by DMA (`setRxBufferSize()`). |
| [ButtonDebounce](ButtonDebounce/README.md) | Filter switch bounce and count a new press once, without blocking waits. |
| [AnalogInputToPwm](AnalogInputToPwm/README.md) | Convert potentiometer readings to PWM duty with explicit ADC and output scales. |
| [BoardWeActCH32X035](BoardWeActCH32X035/README.md) | A starting point combining the peripherals exposed by the WeAct CH32X035 Core Board, with explicit pin names. |
| [BoardUIAPduinoProMicro](BoardUIAPduinoProMicro/README.md) | Initialize UART, I2C, SPI, ADC and PWM using the UIAPduino Pro Micro board pin assignments. |

Verify examples separately before combining them. Delays and extensive demonstration logging are not intended as the final structure for many concurrent features. Continue with the [sketch composition guide (Japanese)](../../../docs/sketch-guide.ja.md) for time management, ISR data transfer and callback handling.

See the [example guide](../../../docs/examples.md) for all libraries and [build rules (Japanese)](../../../docs/examples-build-rules.ja.md) for profiles.
