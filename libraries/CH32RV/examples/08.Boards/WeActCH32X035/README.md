# WeActCH32X035

[日本語](README.ja.md)

The smallest start for the WeAct CH32X035 Core Board: blink the LED (PB12) and print one line a second
on the board's USB-C serial port (`SerialUSB`). One USB cable uploads it and shows the output.

## Hardware and connections

A WeAct CH32X035 Core Board (CH32X035F8U6) and a USB-C cable. Select the board "Generic CH32X035" and
the Part Number CH32X035F8U6.

## Uploading

- **The first time:** pick the port `ch32rv-bootloader://WAIT_FOR_BOOT`, press Upload, and once it waits,
  plug the USB in while holding the DOWNLOAD key (the factory USB ISP returns to the app after about
  10 s without an upload, so enter it after pressing Upload).
- **From then on:** while this sketch runs, the port list shows `ch32rv-app://...` (labelled
  "CH32X035F8U6 (CH32RV, USB)"). Pick it and press Upload: no DOWNLOAD key. Picking the same USB's
  serial port (`COMx`, `/dev/ttyACM...`) uploads the same way.

See [upload routes (Japanese)](../../../../../docs/upload-routes.ja.md).

## Steps and expected results

The LED flashes twice, briefly, then pauses, once a second. Open the serial monitor on the USB serial port (or the
`ch32rv-app://` port): `hello from WeAct CH32X035 0`, `1`, ... arrive once a second.

## Board pins

| Function | Pads | Notes |
|---|---|---|
| LED | PB12 | active High |
| UART (`Serial`) | TX PA2, RX PA3 | USART2 |
| UART (`Serial1`) | TX PB0, RX PB1 | USART4 |
| SPI | SCK PA5, MISO PA6, MOSI PA7, CS PA4 | CS is a plain GPIO |
| ADC | A0 / PA0, A1 / PA1 | |
| USB data | D- PC16, D+ PC17 | used by `SerialUSB` |
| USB PD | CC1 PC14, CC2 PC15 | [CH32RVUsbPd](../../../../CH32RVUsbPd/README.md) |
| Debug | PC18 / PC19 | for a WCH-Link or an OEP probe |

I2C exists only on the USB data pins or the debug pins. UART, SPI, ADC, PWM, PD and I2C together are in
[WeActCH32X035Peripherals](../WeActCH32X035Peripherals/README.md).

## Related documentation

- [Sketch](WeActCH32X035.ino)
- [Basic example setup and index](../../README.md)
- [USB serial examples (CH32RVUsbDevice)](../../../../CH32RVUsbDevice/examples/README.md)
- [Example guide](../../../../../docs/examples.md)
