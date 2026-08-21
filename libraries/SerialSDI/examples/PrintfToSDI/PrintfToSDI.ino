/* PrintfToSDI - send printf() to the debug probe instead of the UART.
 *
 * Wiring: none for the SDI half; the UART half needs the usual serial adapter
 * if you want to see the first line.
 *
 * printf() normally reaches the board's monitor port. ch32_set_stdout() moves
 * it to anything that derives from Print, which is how a debug channel can
 * take over stdio without the core knowing that channel exists.
 *
 * Note what does *not* change: the name Serial still means the UART. This
 * moves stdio, not the Serial object.
 */
#include <SerialSDI.h>
#include <stdio.h>

void setup()
{
    Serial.begin(115200);
    SerialSDI.begin(115200);

    printf("this line goes to the UART\n");

    ch32_set_stdout(&SerialSDI);
    printf("and this one goes to the debug probe\n");

    /* Back again, and then off entirely. */
    ch32_set_stdout(&Serial);
    printf("back on the UART\n");
}

void loop()
{
    static uint32_t n;
    printf("tick %lu\n", (unsigned long)n++);
    delay(1000);
}
