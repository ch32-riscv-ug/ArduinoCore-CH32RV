/* HelloSDI - print through the debug probe, with no UART and no wiring.
 *
 * Wiring: none beyond the WCH-LinkE you already flash with. The target writes
 * into the debug module's data registers and the probe forwards them to its
 * own USB serial port, so this costs no pin and never halts the core.
 *
 * The host has to be told to collect it, which probe-rs cannot do today. With
 * wlink (https://github.com/ch32-rs/wlink), firmware 2.10 or newer:
 *
 *   wlink flash --enable-sdi-print --watch-serial <firmware.elf>
 *
 * Supported on V003/V00x/V103/V20x/V30x/X035/L103. Nothing is lost if no host
 * is listening: the write is dropped after a bounded wait, so unplugging the
 * debugger does not hang the sketch.
 */
#include <SerialSDI.h>

void setup()
{
    SerialSDI.begin(115200);      /* the baud rate is ignored - there is no wire */
    SerialSDI.println("hello from the debug module");
}

void loop()
{
    SerialSDI.print("uptime ");
    SerialSDI.print(millis() / 1000);
    SerialSDI.println(" s");
    delay(1000);
}
