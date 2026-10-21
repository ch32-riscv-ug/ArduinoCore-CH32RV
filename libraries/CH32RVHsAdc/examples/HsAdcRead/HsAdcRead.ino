/* One HSADC conversion a second on PC0, printed in counts and millivolts.
 *
 * PC0 is a VIO18 pin: its input is rated to VIO18 + 0.3 V, and VIO18 starts
 * at 1.2 V on the CH32H417 evaluation board. This sketch raises VIO18 to
 * 3.3 V first - only do that when nothing on the board's VIO18 is a 1.8 V part
 * (or choose 3.3 V in Tools > VIO18 Supply instead). PF8 needs none of this. *
 * board: CH32H417 */
#include <CH32RV.h>
#include <CH32RVHsAdc.h>

CH32RVHsAdc hsadc;

void setup()
{
    Serial.begin(115200);
    CH32RV.setVio18Millivolts(3300);
    if (!hsadc.begin(PC0, 20000000)) {
        Serial.println("PC0 is no HSADC input");
    }
    Serial.printf("HSADC at %lu samples/s\r\n", (unsigned long)hsadc.sampleRate());
}

void loop()
{
    const uint16_t v = hsadc.read();
    Serial.printf("PC0 %4u  %4lu mV\r\n", v, (unsigned long)v * CH32RV.getVddMillivolts() / 1023u);
    delay(1000);
}
