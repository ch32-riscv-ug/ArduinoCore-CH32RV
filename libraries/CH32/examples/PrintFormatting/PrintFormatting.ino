/* PrintFormatting - everything Print and String can do, in one place.
 *
 * Wiring: none.
 *
 * Print is the base of Serial, and of every other output this core has
 * (SerialSDI, and later USB CDC), so what works here works there.
 *
 * dtostrf() comes from the AVR compatibility headers, spelled the way an AVR
 * sketch spells it.
 *
 * **Does not fit CH32V003.** dtostrf() drags in the full formatter with float
 * support, which is about 14 KB more than a 16 KB part has. Everything else
 * here fits; it is that one call that does not. Serial.print(float) is the
 * cheaper way to print a float, and it works on every part.
 */
#include <avr/dtostrf.h>
void setup()
{
    Serial.begin(115200);
    while (!Serial) {
    }

    /* Integers in another base. HEX is uppercase and carries no 0x prefix. */
    Serial.print("255 -> ");
    Serial.print(255, DEC);
    Serial.print(' ');
    Serial.print(255, HEX);
    Serial.print(' ');
    Serial.print(255, OCT);
    Serial.print(' ');
    Serial.println(255, BIN);

    /* Floats: the second argument is the number of decimals, default 2. */
    Serial.print("pi -> ");
    Serial.print(3.14159, 0);
    Serial.print(' ');
    Serial.print(3.14159, 2);
    Serial.print(' ');
    Serial.println(3.14159, 4);

    /* dtostrf() formats into a buffer instead, which is how you get a fixed
     * width. It comes from AVR and is kept for the sketches that expect it. */
    char buffer[16];
    dtostrf(3.14159, 8, 3, buffer);
    Serial.print("dtostrf: [");
    Serial.print(buffer);
    Serial.println(']');

    /* String builds text on the heap. Convenient, and the usual way a small
     * part runs out of RAM - CH32V003 has 2 KB in total. */
    String s = "count: ";
    s += 42;
    s += ", hex ";
    s += String(255, HEX);
    Serial.println(s);

    /* write() sends raw bytes; print() sends text. The difference matters. */
    Serial.print("65 as text: ");
    Serial.println(65);
    Serial.print("65 as a byte: ");
    Serial.write(65);
    Serial.println();

    /* println() with no argument is just the line ending. */
    Serial.println();
    Serial.println("done");
}

void loop()
{
}
