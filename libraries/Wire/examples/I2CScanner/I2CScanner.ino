/* I2CScanner - list the devices that answer on the bus.
 *
 * Wiring: SDA and SCL to the device, a common ground, and **pull-up resistors**
 * (4.7k to 3V3 on each line) unless the module already has them. Without
 * pull-ups nothing answers and every address reports an error - which this
 * sketch shows as a timeout rather than as silence.
 *
 * The pins come from the variant: SDA and SCL are the first I2C bus's default
 * route, and the sketch prints them so you know where to put the wires. Some
 * packages do not bond that route - use Wire.setPins() to move the bus if the
 * pads printed here are not on your board's headers.
 */
#include <Wire.h>

void setup()
{
    Serial.begin(115200);
    while (!Serial) {
    }
    Wire.begin();

    Serial.println();
    Serial.print("scanning I2C on SCL=pad ");
    Serial.print(SCL);
    Serial.print(", SDA=pad ");
    Serial.println(SDA);

    unsigned found = 0;
    for (uint8_t address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        const uint8_t result = Wire.endTransmission();
        if (result == 0) {
            Serial.print("  device at 0x");
            Serial.println(address, HEX);
            found++;
        } else if (result == 5) {
            /* Timeout: the bus never released, which usually means no
             * pull-ups rather than no device. Say it once and stop. */
            Serial.println("  bus timed out - are the pull-ups fitted?");
            return;
        }
    }
    Serial.print("done, ");
    Serial.print(found);
    Serial.println(" device(s)");
}

void loop()
{
}
