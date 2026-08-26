/* HelloRTT - print through the debug probe, with no UART and no wiring.
 *
 * Wiring: none beyond the WCH-LinkE you already flash with. The sketch writes
 * into a ring buffer in RAM and the probe reads that memory while the core
 * runs, so this costs no pin and never halts the core.
 *
 * The host side is probe-rs, which is what this core flashes with:
 *
 *   probe-rs attach --chip CH32V003F4P6 <firmware.elf>
 *
 * (any pnum from the board menu works as --chip). The ELF matters: that is
 * where probe-rs finds the control block.
 *
 * Nothing is lost if no host is attached - the buffer fills and further bytes
 * are dropped, the way a UART with nobody listening loses them.
 */
#include <SerialRTT.h>

void setup()
{
    SerialRTT.begin(115200);      /* the baud rate is ignored - there is no wire */
    SerialRTT.println("hello from RAM");
}

void loop()
{
    SerialRTT.print("uptime ");
    SerialRTT.print(millis() / 1000);
    SerialRTT.println(" s");
    delay(1000);
}
