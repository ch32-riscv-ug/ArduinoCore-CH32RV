# PPVisionCH32X035

[日本語](README.ja.md)

The board from [ppvision/CH32X035](https://github.com/ppvision/CH32X035): a CH32X035G8U6 (QFN28) with a 0.96"
160x80 ST7735 LCD, two side buttons and four LEDs. The sketch draws on the LCD with
[Adafruit's ST7735 library](https://github.com/adafruit/Adafruit-ST7735-Library), counts the buttons' presses on it
and lights the LEDs one after another.

## Pins

The board publishes no schematic. The pins below come from its sample sources (`Apps/gpio`, `Apps/FreeRTOS`,
`components/lcd`); the LEDs' and buttons' places on the board are not confirmed.

| Function | Pin | Source |
|---|---|---|
| LCD SCK / MOSI (SPI1) | PA5 / PA7 | `components/lcd` (the core's default SPI pins) |
| LCD CS / DC / RST | PB4 / PB1 / PB0 | `components/lcd`; no backlight control |
| Button UP / DOWN (to GND, pull-up) | PB8 / PB6 | `Apps/gpio` EXTI, falling edge |
| LEDs | PA0, PA1, PC0, PC3 | `Apps/gpio` toggles them as outputs |
| SPI flash CS (not used here) | PB3 | commented `SPI_CS2` |
| Header TX2 / RX2 | PA2 / PA3 (USART2) | `Apps/SimulateCDC` |

The LCD is the "plugin" 160x80 panel: offsets 26 / 1, BGR, not inverted, as the board's own driver sets it.

The Adafruit libraries are not bundled with the core: install "Adafruit ST7735 and ST7789 Library" from the Library
Manager (it brings Adafruit GFX and BusIO; checked with 1.12.0 / 2.0.0 / 1.17.4, and a build from the sketch.yaml
profile installs those versions by itself).

## Uploading and operation

- **Board menu:** CH32X035, Part Number CH32X035G8U6.
- **Upload:** with a WCH-LinkE on the 3V3 / GND / DIO / DCK pads (SWDIO PC18, SWCLK PC19). The sketch keeps a USB
  device (`SerialUSB`) up, so the next upload can also go to the board's `ch32rv-app://` port or serial port.
- **What it shows:** `CH32X035 + ST7735` at the top, a count in the middle, UP / DOWN boxes on the right that fill
  while the button is held. UP adds one, DOWN takes one away, and each change is printed on the USB serial port.
  The four LEDs light in turn every 200 ms.

## Changing it

- If the colours come out inverted (black shown white), change `tft.invertDisplay(true)` to `false`. If the picture
  is shifted by a few pixels, the panel is another variant: try `INITR_MINI160x80` instead.
- `tft.setRotation(3)` turns the picture upside down; 0 / 2 are portrait (80x160).

## Related

- [Sketch](PPVisionCH32X035.ino)
- [ppvision/CH32X035](https://github.com/ppvision/CH32X035): the board's own sources and pictures
