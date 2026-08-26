/* HelloDMDATA - a two-way terminal over the debug probe, with no UART and no
 * wiring.
 *
 * Wiring: none beyond the WCH-LinkE you already flash with. The sketch writes
 * the debug module's data registers and the probe carries them, so this costs
 * no pin, no RAM buffer, and never halts the core.
 *
 * The host side is minichlink's terminal, from ch32fun
 * (https://github.com/cnlohr/ch32fun) - this core does not ship it:
 *
 *   minichlink -T
 *
 * Type a line and it comes back uppercased. Do not use SerialSDI in the same
 * sketch: it writes the same two registers in a different framing.
 */
#include <SerialDMDATA.h>

void setup()
{
    SerialDMDATA.begin(115200);   /* the baud rate is ignored - there is no wire */
    SerialDMDATA.println("hello from the debug module");
}

void loop()
{
    /* Both directions share one register, so what the host sent has to be
     * taken out of it before the next print overwrites it. available() does
     * that - calling it every time round the loop is the whole trick. */
    while (SerialDMDATA.available() > 0) {
        int c = SerialDMDATA.read();
        if (c >= 'a' && c <= 'z') {
            c -= 'a' - 'A';
        }
        SerialDMDATA.write((uint8_t)c);
    }

    static unsigned long last;
    if (millis() - last >= 1000) {
        last = millis();
        SerialDMDATA.print("uptime ");
        SerialDMDATA.print(millis() / 1000);
        SerialDMDATA.println(" s");
    }
}
