/* I2CScanner - list the devices that answer on the bus.
 *
 * Wiring: SDA and SCL to the device, a common ground, and **pull-up resistors**
 * (4.7k to 3V3 on each line) unless the module already has them. Without
 * pull-ups nothing answers and every address reports an error - which this
 * sketch shows as a timeout rather than as silence.
 *
 * The pads below are the chip's default ones for its first I2C (SDA, SCL) and
 * first USART (RX, TX). Some packages do not bond them - change them to the
 * pads your board wires: each bus takes whichever enabled peripheral has them
 * as a route.
 */
#include <Wire.h>

static const int SDA_PIN = SDA;
static const int SCL_PIN = SCL;
static const int RX_PIN = RX;
static const int TX_PIN = TX;

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
    while (!Serial) {
    }
    Wire.begin(SDA_PIN, SCL_PIN);

    Serial.println();
    Serial.print("scanning I2C");
    Serial.print(Wire.peripheral());
    Serial.print(" on SCL=pad ");
    Serial.print(SCL_PIN);
    Serial.print(", SDA=pad ");
    Serial.println(SDA_PIN);

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
