/* RttEcho - the debug channel in both directions.
 *
 * Unlike SerialSDI, this one can be typed into: the host writes a second ring
 * buffer and the sketch reads it. probe-rs sends what you type when it is
 * attached to a terminal channel:
 *
 *   probe-rs attach --chip CH32V003F4P6 <firmware.elf>
 *
 * Type a line and it comes back uppercased. Nothing here blocks, so the LED
 * keeps blinking whether or not a host is attached - which is the point of
 * checking available() rather than waiting for input.
 *
 * The blink needs an LED_BUILTIN, which only a product board or
 * -DLED_BUILTIN defines; without one the echo still works.
 */
#include <SerialRTT.h>

void setup()
{
#ifdef LED_BUILTIN
    pinMode(LED_BUILTIN, OUTPUT);
#endif
    SerialRTT.begin(115200);
    SerialRTT.println("type a line; it comes back in capitals");
}

void loop()
{
    while (SerialRTT.available() > 0) {
        int c = SerialRTT.read();
        if (c >= 'a' && c <= 'z') {
            c -= 'a' - 'A';
        }
        SerialRTT.write((uint8_t)c);
    }

#ifdef LED_BUILTIN
    digitalWrite(LED_BUILTIN, (millis() / 500) % 2 == 0 ? HIGH : LOW);
#endif
}
