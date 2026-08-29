/* SoftI2CScanner - find the devices on a bit-banged I2C bus.
 *
 * Wiring: SDA and SCL to the device, and **pull-up resistors to 3V3** - 4.7k
 * is the usual value. Nothing here can substitute for them: I2C is open-drain,
 * so without pull-ups the lines never rise and every address times out.
 *
 * The two pads below are the sketch's own choice - that is the whole point of
 * this library, so change them to whatever your board leaves free. PA1 and PA2
 * are the only two pad names every CH32 series has, which is the only reason
 * they are the ones here.
 */
#include <SoftWire.h>

static const uint8_t SDA_PIN = PA1;
static const uint8_t SCL_PIN = PA2;

SoftWire bus(SDA_PIN, SCL_PIN);

void setup()
{
    Serial.begin(115200);
    while (!Serial) {
    }
    bus.begin();
    bus.setClock(100000);

    Serial.println();
    Serial.print("SoftWire on SDA=pad ");
    Serial.print(SDA_PIN);
    Serial.print(", SCL=pad ");
    Serial.println(SCL_PIN);
}

void loop()
{
    uint8_t found = 0;

    /* 0x00-0x07 and 0x78-0x7F are reserved by the I2C specification. */
    for (uint8_t address = 0x08; address < 0x78; address++) {
        bus.beginTransmission(address);
        if (bus.endTransmission() == 0) {
            Serial.print("found 0x");
            if (address < 0x10) {
                Serial.print('0');
            }
            Serial.println(address, HEX);
            found++;
        }
    }

    if (found == 0) {
        Serial.println(bus.getWireTimeoutFlag()
                           ? "nothing found, and the bus timed out - pull-ups?"
                           : "nothing found");
        bus.clearWireTimeoutFlag();
    }
    delay(2000);
}
