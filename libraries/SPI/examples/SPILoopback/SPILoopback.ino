/* SPILoopback - send bytes and read them back through a jumper.
 *
 * Wiring: one jumper from MOSI to MISO. That is the whole test rig: whatever
 * the master sends comes straight back, so a correct byte proves the clock,
 * the data line and the framing all work.
 *
 * Without the jumper MISO is pulled up and every byte reads back 0xFF, which
 * the sketch reports as "no loopback" rather than as a failure.
 *
 * The pads below are the chip's default ones for its first SPI (SCK, MISO,
 * MOSI) and first USART (RX, TX). Change them to the pads your board wires:
 * each bus takes whichever enabled peripheral has them as a route.
 */
#include <SPI.h>

static const int SCK_PIN = SCK;
static const int MISO_PIN = MISO;
static const int MOSI_PIN = MOSI;
static const int RX_PIN = RX;
static const int TX_PIN = TX;

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
    while (!Serial) {
    }
    SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, -1);

    Serial.println();
    Serial.print("SPI");
    Serial.print(SPI.peripheral());
    Serial.print(" on SCK=pad ");
    Serial.print(SCK_PIN);
    Serial.print(", MISO=pad ");
    Serial.print(MISO_PIN);
    Serial.print(", MOSI=pad ");
    Serial.println(MOSI_PIN);
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
