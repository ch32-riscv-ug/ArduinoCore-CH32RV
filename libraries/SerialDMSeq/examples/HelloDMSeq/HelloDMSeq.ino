/* HelloDMSeq - a two-way terminal over the debug probe, with no UART and no
 * wiring, where no byte is lost or doubled on the way.
 *
 * Wiring: none beyond the probe you already flash with. The sketch writes the
 * debug module's data registers and the probe carries them, so this costs no
 * pin and never halts the core.
 *
 * On the host:
 *
 *   ch32rv monitor --source dmseq
 *
 * Type a line and it comes back uppercased. Do not use SerialSDI or
 * SerialDMDATA in the same sketch: they write the same two registers in a
 * different framing.
 */
#include <SerialDMSeq.h>

void setup()
{
    SerialDMSeq.begin(115200);   /* the baud rate is ignored - there is no wire */
    SerialDMSeq.println("hello from the debug module");
}

void loop()
{
    /* Input arrives in the host's answers. available() is what invites one when
     * the sketch has nothing to print, so call it every time round the loop. */
    while (SerialDMSeq.available() > 0) {
        int c = SerialDMSeq.read();
        if (c >= 'a' && c <= 'z') {
            c -= 'a' - 'A';
        }
        SerialDMSeq.write((uint8_t)c);
    }

    static unsigned long last;
    if (millis() - last >= 1000) {
        last = millis();
        SerialDMSeq.print("uptime ");
        SerialDMSeq.println(millis() / 1000);
    }
}
