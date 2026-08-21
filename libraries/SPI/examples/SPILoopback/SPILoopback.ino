/* SPILoopback - send bytes and read them back through a jumper.
 *
 * Wiring: one jumper from MOSI to MISO. That is the whole test rig: whatever
 * the master sends comes straight back, so a correct byte proves the clock,
 * the data line and the framing all work.
 *
 * Without the jumper MISO is pulled up and every byte reads back 0xFF, which
 * the sketch reports as "no loopback" rather than as a failure.
 */
#include <SPI.h>

void setup()
{
    Serial.begin(115200);
    while (!Serial) {
    }
    SPI.begin();

    Serial.println();
    Serial.print("SPI on SCK=pad ");
    Serial.print(SCK);
    Serial.print(", MISO=pad ");
    Serial.print(MISO);
    Serial.print(", MOSI=pad ");
    Serial.println(MOSI);
    Serial.println("connect MOSI to MISO for the loopback");
}

void loop()
{
    static const uint8_t pattern[] = {0x00, 0x55, 0xAA, 0xFF, 0x5A};

    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    for (unsigned i = 0; i < sizeof pattern; i++) {
        const uint8_t got = SPI.transfer(pattern[i]);
        Serial.print("sent 0x");
        Serial.print(pattern[i], HEX);
        Serial.print("  got 0x");
        Serial.print(got, HEX);
        Serial.println(got == pattern[i] ? "  ok" : "  (no loopback)");
    }
    SPI.endTransaction();

    Serial.println();
    delay(2000);
}
