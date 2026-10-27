# Example sketch guide

[日本語](examples.ja.md)

Start with a small example, confirm wiring and I/O, then combine functions. Every sketch has a neighboring README describing hardware, operation, expected results, API use and adaptation notes.

## Choose by purpose

| Purpose | Library and guide | Start here |
|---|---|---|
| Basic I/O | [CH32RV](../libraries/CH32RV/examples/README.md) | PinCapabilities → Blink / SerialEcho → Timing |
| Hardware I2C | [Wire](../libraries/Wire/examples/I2CScanner/README.md) | I2CScanner → I2CRegisterRead / I2CSlaveRegister → I2CTimeoutRecovery |
| Software I2C | [SoftWire](../libraries/SoftWire/examples/SoftI2CScanner/README.md) | SoftI2CScanner |
| Hardware SPI | [SPI](../libraries/SPI/examples/SPILoopback/README.md) | SPILoopback → SpiFlashId |
| Software SPI | [SoftSPI](../libraries/SoftSPI/examples/SoftSPILoopback/README.md) | SoftSPILoopback |
| Servo control | [Servo](../libraries/Servo/examples/Sweep/README.md) | Sweep |
| ISR data transfer | [CH32RVSyncKit](../libraries/CH32RVSyncKit/examples/README.md) | QueueFromInterrupt / DeadlinePeriod |
| Combining work | [CH32RVTaskKit](../libraries/CH32RVTaskKit/examples/README.md) | BasicWork → ButtonBlinkConsole |
| A USB device connected to a PC | [CH32RVUsbDevice](../libraries/CH32RVUsbDevice/examples/README.md) | SerialUSBEcho → class examples |
| USB peripherals connected to the board | [CH32RVUsbHost](../libraries/CH32RVUsbHost/examples/README.md) | DeviceInfo → class examples |
| Type-C power negotiation | [CH32RVUsbPd](../libraries/CH32RVUsbPd/examples/README.md) | ListProfiles / CycleVoltages |
| Continuous ADC capture | [CH32RVAdcStream](../libraries/CH32RVAdcStream/README.md) | AdcStreamStats |
| High-speed ADC | [CH32RVHsAdc](../libraries/CH32RVHsAdc/README.md) | HsAdcRead → HsAdcStream |
| GPIO capture | [CH32RVLogicCapture](../libraries/CH32RVLogicCapture/README.md) | LogicBurst → LogicStream |
| Debug text output | [SerialSDI](../libraries/SerialSDI/README.md) | HelloSDI / PrintfToSDI |
| Bidirectional debug terminal | [SerialDMDATA](../libraries/SerialDMDATA/README.md) | HelloDMDATA |
| Bidirectional debug terminal | [SerialDMSeq](../libraries/SerialDMSeq/README.md) | HelloDMSeq |
| RAM-buffer terminal | [SerialRTT](../libraries/SerialRTT/README.md) | HelloRTT / RttEcho |

Distinguish USB roles first: Device connects as a peripheral to a PC or another host; Host controls connected peripherals; PD negotiates power. A USB-UART adapter for diagnostics is separate from the MCU's own USB data connection.

## Further UART and system examples

After `SerialEcho`, try the fixed-buffer [line receiver](../libraries/CH32RV/examples/SerialLineReceiver/README.md) and [binary packet receiver](../libraries/CH32RV/examples/SerialPacketReceiver/README.md) to process input incrementally.
[SystemInfo](../libraries/CH32RV/examples/SystemInfo/README.md), [ResetReason](../libraries/CH32RV/examples/ResetReason/README.md) and [WatchdogRecovery](../libraries/CH32RV/examples/WatchdogRecovery/README.md) introduce system APIs using only UART.
After `Fade`, use [PwmFrequency](../libraries/CH32RV/examples/PwmFrequency/README.md) to distinguish frequency from duty.

## From physical input to USB HID

Start with [ButtonDebounce](../libraries/CH32RV/examples/ButtonDebounce/README.md), then use
[ButtonKeyboard](../libraries/CH32RVUsbDevice/examples/ButtonKeyboard/README.md) or [ButtonMouse](../libraries/CH32RVUsbDevice/examples/ButtonMouse/README.md) to turn stable switch state into host input.
After [AnalogInputToPwm](../libraries/CH32RV/examples/AnalogInputToPwm/README.md) introduces numeric scaling,
[AnalogGamepad](../libraries/CH32RVUsbDevice/examples/AnalogGamepad/README.md) adds two-axis center calibration and a dead zone.

## Run an example

1. Read the sketch and its README. Check the exact part, power, input voltage and pins.
2. Choose the board and Part Number, then adapt pin constants to the PCB.
3. Upload using an appropriate [upload route (Japanese)](upload-routes.ja.md).
4. Open the monitor or class application and compare the steps with the expected results.
5. Change one thing at a time. Check failure returns, disconnects and absent-input behavior too.

In Arduino IDE, open File → Examples → library name. For CLI builds, replace the board selection and sketch path in the common guide's command. Profiles describe build conditions, not verified hardware operation; see [example build rules (Japanese)](examples-build-rules.ja.md).

## Before combining examples

| Check | Why |
|---|---|
| Pins, power and external circuits | Shared pads, voltage domains and motor supply conditions cannot be solved in software alone |
| Units | Distinguish ms/µs, Hz, mV/mA, samples and bytes |
| Return values | Distinguish boolean success, actual processed length and `-1` for no received byte |
| Object and buffer lifetime | Keep functions, callback targets, DMA and direct-transfer buffers alive and unchanged while used |
| Execution context | Distinguish ISR, `loop()`, notification callbacks and response providers |
| Waiting and retries | Demonstration delays and verbose output can hold up other input or transfer work |

The [sketch composition guide (Japanese)](sketch-guide.ja.md) explains elapsed-time checks, ISR handoff, `deliverEvents()` and short work functions. For supported series and implementation limits, use the library README, [USB support table](usb.ja.md#対応状況) and [peripheral guide](peripheral-support.ja.md), rather than duplicating them in every example.
