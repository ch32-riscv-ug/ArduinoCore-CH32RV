# SerialMultiprocessor

[日本語](README.ja.md)

Several boards on one UART line, called by address (the USART's mute mode, `setAddress()`). The controller sends an
address frame and then data; only the node with that address takes the data.

## What you need and wiring

Two or more boards with this sketch. Set `MY_ADDRESS` to 0 on one (the controller) and 1, 2, 3 on the others.
The controller's TX to every node's RX, GND common. The nodes' TX stay unconnected. Each node lights its LED
(`LED_BUILTIN`, or an LED with a resistor on a pin such as `A0`).

## Operation and expected result

After power-up the LEDs of nodes 1, 2 and 3 light in turn, half a second each. The controller sends a value to all
three every time, but each node takes only the bytes after its own address, so a `'1'` for another node does not
light it.

## API and flow

`setAddress(a)` (0 .. 15) makes 8N1's words 9 bits: the ninth is 1 on an address frame and 0 on data. A receiver
takes data only after an address frame whose low 4 bits are its address, and mutes itself at an address frame for
anyone else. The address frames never reach `read()`. `writeAddress(a)` sends one address frame (what was
`write()`n before goes out first). `setAddress(-1)` goes back to plain UART.

`mute()` stops receiving until the next address frame for this port; without `setAddress()`, until the line has
been quiet for a frame (to skip the rest of a message); on a quiet line it wakes at once, so call it while the message
is still coming.

`setAddress()` returns false with parity (8E1 and so on; the ninth bit is the parity's place) and with 5 or 6 data
bits, and with `setRxBufferSize()`'s DMA receive (the DMA would read the address frames too).

## Changing it

For nodes that answer, make their TX open drain (or put a diode in each) joined to the controller's RX, and answer
to address 0. With an RS485 transceiver, combine it with `setMode(UART_MODE_RS485_HALF_DUPLEX)`.

## Related documents

- [Sketch](SerialMultiprocessor.ino)
- [Examples: setup and list](../../README.md)
