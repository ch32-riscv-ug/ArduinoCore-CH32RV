# Basic examples

[日本語](README.ja.md)

Small examples for GPIO, UART, ADC, PWM and interrupts. Each folder's README covers connections, operation, API roles and adaptation notes.

## Preparation

In Arduino IDE, choose File → Examples → CH32RV and select the board and exact Part Number. Generic boards do not define PCB wiring; adapt the pin constants at the top of each `.ino` to your schematic. Start with PinCapabilities to inspect aliases such as `A1` or `RX`.

Most examples log through hardware `Serial`. Connect adapter RX to board TX, TX to RX, and common GND. Match I/O voltage and `Serial.begin()` settings. A USB connector alone does not mean that UART output can be read through it.

With the core installed, compile from the repository root:

```sh
arduino-cli compile --fqbn ch32-riscv-ug:ch32rv:CH32X035:part=CH32X035F8U6 \
  libraries/CH32RV/examples/01.Basics/Timing
```

Blink needs the actual board LED assigned to `LED_BUILTIN`. Use a product-board definition when provided. For a Generic board, define it in the sketch or pass the actual pin through a build option.

## Choose an example

### 01.Basics

| Example | What it teaches |
|---|---|
| [Blink](01.Basics/Blink/README.md) | Toggle a GPIO to confirm sketch execution with an LED. |
| [Fade](01.Basics/Fade/README.md) | Vary PWM duty to fade an LED up and down. |
| [PrintFormatting](01.Basics/PrintFormatting/README.md) | Compare integer, floating-point and string formatting with raw byte output. |
| [RandomNumbers](01.Basics/RandomNumbers/README.md) | Compare reproducible pseudorandom sequences with one seeded from `ch32rv_random()`. |
| [ShiftOut](01.Basics/ShiftOut/README.md) | Send bytes to a shift register and activate its outputs in sequence. |
| [Timing](01.Basics/Timing/README.md) | Demonstrate `millis()`, `micros()` and elapsed-time subtraction across wraparound. |
| [ToneMelody](01.Basics/ToneMelody/README.md) | Play a scale on a passive buzzer by specifying frequency and duration. |
| [ButtonDebounce](01.Basics/ButtonDebounce/README.md) | Filter switch bounce and count a new press once, without blocking waits. |
| [AnalogInputToPwm](01.Basics/AnalogInputToPwm/README.md) | Convert potentiometer readings to PWM duty with explicit ADC and output scales. |

### 02.GPIO (pins and interrupts)

| Example | What it teaches |
|---|---|
| [PinInterrupt](02.GPIO/PinInterrupt/README.md) | Count GPIO edges in an interrupt and defer printing to `loop()`. |
| [PinCapabilities](02.GPIO/PinCapabilities/README.md) | Inspect variant-defined pins and their ADC mappings. |
| [PulseIn](02.GPIO/PulseIn/README.md) | Generate a trigger and measure the duration of a High input pulse. |
| [RgbLed](02.GPIO/RgbLed/README.md) | Light WS2812 / SK6812 RGB LEDs on any pin with `rgbLedWrite()`. |
| [TouchRead](02.GPIO/TouchRead/README.md) | Read a TouchKey pad with `touchRead()` and report a touch (V00x, X035, V20x, V30x). |

### 03.Analog (ADC and PWM)

| Example | What it teaches |
|---|---|
| [AnalogRead](03.Analog/AnalogRead/README.md) | Observe raw analog input readings and their voltage in mV on UART. |
| [AnalogContinuous](03.Analog/AnalogContinuous/README.md) | Average frames the ADC fills by itself, with arduino-esp32's continuous ADC API (`analogContinuous()`). |
| [InternalAnalogRead](03.Analog/InternalAnalogRead/README.md) | Read the internal reference, the supply voltage and the die temperature. |
| [AnalogResolution](03.Analog/AnalogResolution/README.md) | Change the numeric scales used by ADC and PWM APIs. |
| [PwmFrequency](03.Analog/PwmFrequency/README.md) | Change PWM frequency while keeping the duty scale and value constant. |

### 04.Serial (UART)

| Example | What it teaches |
|---|---|
| [SerialEcho](04.Serial/SerialEcho/README.md) | Receive a line over hardware UART and echo it with surrounding whitespace removed. |
| [SerialBridge](04.Serial/SerialBridge/README.md) | Relay bytes between `Serial` and `Serial1`, to drive a UART module (GPS, Wi-Fi, ...) straight from the PC. |
| [SerialLineReceiver](04.Serial/SerialLineReceiver/README.md) | Receive bounded text lines incrementally, with overflow and timeout handling. |
| [SerialPacketReceiver](04.Serial/SerialPacketReceiver/README.md) | Parse length-prefixed binary packets with a checksum and timeout. |
| [SerialReceiveEvent](04.Serial/SerialReceiveEvent/README.md) | Take a whole message when the line goes quiet (`onReceive()`) and hear about receive errors. |
| [SerialFlowControl](04.Serial/SerialFlowControl/README.md) | Pace a sender with RTS and hold sending with CTS (`setHwFlowCtrlMode()`). |
| [SerialMultiprocessor](04.Serial/SerialMultiprocessor/README.md) | Call several boards on one UART line by address (`setAddress()`). |
| [SerialRS485](04.Serial/SerialRS485/README.md) | Query an RS485 bus; the core switches the transceiver's driver enable around each write. |
| [SerialHalfDuplex](04.Serial/SerialHalfDuplex/README.md) | Send and receive on one open-drain wire (`setPins(p, p)`). |
| [SerialDmaBuffers](04.Serial/SerialDmaBuffers/README.md) | Take a 1 Mbaud stream while `loop()` is busy: larger buffers moved by DMA (`setRxBufferSize()`). |

### 05.Timer (timers and interrupts)

| Example | What it teaches |
|---|---|
| [CriticalSection](05.Timer/CriticalSection/README.md) | Demonstrate copying interrupt-shared values inside a short interrupt-disabled section. |
| [TimerAlarm](05.Timer/TimerAlarm/README.md) | Run a function every second with arduino-esp32's timer API (`timerBegin()`, `timerAlarm()`). |
| [InterruptPriority](05.Timer/InterruptPriority/README.md) | Make a slow timer interrupt less urgent so `millis()` keeps counting through it. |
| [TimerResource](05.Timer/TimerResource/README.md) | An advanced timer-ownership example for library authors: acquire a timer, set its base and attach an update ISR. |

### 06.System (system and storage)

| Example | What it teaches |
|---|---|
| [LowVoltageDetect](06.System/LowVoltageDetect/README.md) | Warn when the supply falls below a threshold (the PVD's EXTI line), and raise the line from software. |
| [Crc32](06.System/Crc32/README.md) | Check data with `CH32RV.crc32()`, in one call or in pieces. |
| [SystemInfo](06.System/SystemInfo/README.md) | Print the build target, memory information and optional chip identity. |
| [ResetReason](06.System/ResetReason/README.md) | Compare the current reset reason with a command-triggered software reset. |
| [WatchdogRecovery](06.System/WatchdogRecovery/README.md) | Start the watchdog on command and observe a reset after feeding stops. |
| [WindowWatchdog](06.System/WindowWatchdog/README.md) | Start the window watchdog on command and see it reset on a late and on an early feed. |
| [RtcAlarm](06.System/RtcAlarm/README.md) | Keep the date in the RTC, ring an alarm, sleep until it, and count boots in a backup register. |
| [FlashSettings](06.System/FlashSettings/README.md) | Save settings in one page of the internal flash and read them back after a reset (advanced; an external SD card or EEPROM is recommended). |
| [FlashSettingsTwoBanks](06.System/FlashSettingsTwoBanks/README.md) | Save to two pages in turn so a power cut while saving keeps the previous settings (advanced). |

### 07.Chips (one chip only)

| Example | What it teaches |
|---|---|
| [DualCoreV3F](07.Chips/CH32H417/DualCoreV3F/README.md) | CH32H417: run `setup1()` / `loop1()` on the V3F while the sketch runs on the V5F. |
| [DualCoreMessages](07.Chips/CH32H417/DualCoreMessages/README.md) | CH32H417: the V5F and the V3F talk through messages (IPC) and a hardware semaphore (HSEM). |

### 08.Boards (one board)

| Example | What it teaches |
|---|---|
| [WeActCH32X035](08.Boards/WeActCH32X035/README.md) | The smallest start for the WeAct CH32X035 Core Board: blink the LED and print on its USB-C serial port (USB alone uploads and shows it). |
| [WeActCH32X035Peripherals](08.Boards/WeActCH32X035Peripherals/README.md) | USB, PD, two UARTs, SPI, ADC and PWM together on the same board. |
| [WeActCH32X035OledSH1106](08.Boards/WeActCH32X035OledSH1106/README.md) | An SH1106 OLED on the WeAct CH32X035's PC18 / PC19, drawn with U8g2 (the debug pads released, uploads over USB). |
| [UIAPduinoProMicro](08.Boards/UIAPduinoProMicro/README.md) | Blink the UIAPduino Pro Micro's LED by its printed number (USB alone); the README maps the pins and says how to see Serial. |

Verify examples separately before combining them. Examples that wait in `delay()` and extensive demonstration logging are not intended as the final structure for many concurrent features. Continue with the [sketch composition guide (Japanese)](../../../docs/sketch-guide.ja.md) for time management, ISR data transfer and callback handling.

See the [example guide](../../../docs/examples.md) for all libraries and [build rules (Japanese)](../../../docs/examples-build-rules.ja.md) for profiles.
