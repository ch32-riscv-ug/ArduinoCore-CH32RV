/* SoftSPILoopback - bit-banged SPI, checked with one jumper.
 *
 * Wiring: one jumper from MOSI to MISO. Whatever is sent comes straight back,
 * so a correct byte proves the clock, the data line and the framing.
 *
 * Without the jumper MISO floats and the readings are meaningless; the sketch
 * says so rather than calling it a failure.
 *
 * The three pads below are the sketch's own choice - that is the whole point
 * of this library, so change them to whatever your board leaves free. PA1 and
 * PA2 are the only two pad names every CH32 series has, which is the only
 * reason they are the ones here.
 */
#include <SoftSPI.h>

static const uint8_t SCK_PIN = PA1;
static const uint8_t MOSI_PIN = PA2;
#if defined(PA4)
static const uint8_t MISO_PIN = PA4;
#else
static const uint8_t MISO_PIN = PC4;   /* CH32V002 and CH32V003 have no PA4 */
#endif

SoftSPI bus(SCK_PIN, MOSI_PIN, MISO_PIN);

void setup()
{
    Serial.begin(115200);
    while (!Serial) {
    }
    bus.begin();

    Serial.println();
    Serial.print("SoftSPI on SCK=pad ");
    Serial.print(SCK_PIN);
    Serial.print(", MOSI=pad ");
    Serial.print(MOSI_PIN);
    Serial.print(", MISO=pad ");
    Serial.println(MISO_PIN);
    Serial.println("connect MOSI to MISO for the loopback");
}

void loop()
{
    static const uint8_t sent[] = {0x00, 0x55, 0xAA, 0xFF, 0x0F};

    for (uint8_t mode = 0; mode < 4; mode++) {
        bus.beginTransaction(SPISettings(1000000, MSBFIRST, (SPIMode)mode));
        bool ok = true;
        for (unsigned i = 0; i < sizeof sent; i++) {
            if (bus.transfer(sent[i]) != sent[i]) {
                ok = false;
            }
        }
        bus.endTransaction();

        Serial.print("mode ");
        Serial.print(mode);
        Serial.println(ok ? ": loopback ok" : ": no loopback (is the jumper on?)");
    }
    delay(1000);
}
