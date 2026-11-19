# RgbLed

[日本語](README.ja.md)

Light WS2812 / SK6812-type RGB LEDs from any pin with `rgbLedWrite()` (arduino-esp32's name).

## Hardware and connections

Connect the data input (DIN) of the first LED to `LED_PIN` (PA1), and the LEDs' GND to the board's GND. Power the LEDs as their datasheet says. A WS2812B at 5 V expects a high of at least 0.7 x VDD: from a 3.3 V pin use a level shifter (a 74AHCT125 and the like), or power the first LED a diode drop lower. A series resistor of a few hundred ohms near DIN is common.

## Steps and expected results

The chain cycles red, green, blue and off every half second; each LED down the chain is half as bright as the one before it.

## API and processing flow

`rgbLedWrite(pin, r, g, b)` sends one LED's 24 bits, in GRB order, with interrupts off for about 30 us, and returns. For a chain, `ch32rv_rgb_led_write(pin, bytes, len)` sends all the LEDs' bytes (in the order they go out) in one frame: separate `rgbLedWrite()` calls leave the line low between LEDs for the call itself, which at low clocks is long enough to latch (13 us on V006 at 24 MHz). Neither waits for the latch: the colours show once the line has stayed low for the reset time (50 us on WS2812, 280 us on WS2812B-V5 and many newer parts). `rgbLedWriteOrdered(pin, LED_COLOR_ORDER_RGB, r, g, b)` sends another order. `neopixelWrite()` is the older, deprecated name.

## Adapting the example

Interrupts are off for 30 us per LED for the whole chain (100 LEDs: 3 ms). On CH32V4x7 and CH32X3x5, set `LED_PIN` to a pad of the WS2812 block (PA15; on X3x5 also PD3 or PD4): the same calls then send by the block and DMA with interrupts on (`ch32rv_rgb_led_is_hardware(LED_PIN)` says so). Elsewhere, for many LEDs, a library that drives the line with SPI or DMA keeps interrupts on. The timing is in software; check it on a new board with a logic analyzer if the colours look wrong.

## Related documentation

- [Sketch](RgbLed.ino)
- [Basic example setup and index](../README.md)
- [Example guide](../../../../docs/examples.md)
- [Sketch composition guide (Japanese)](../../../../docs/sketch-guide.ja.md)
