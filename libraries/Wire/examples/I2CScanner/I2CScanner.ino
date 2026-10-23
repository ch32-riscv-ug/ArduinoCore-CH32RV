// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

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
        // en: An ACK identifies a responding address, not the device type.
        // ja: ACKで分かるのは応答するaddressであり、機器の種類ではない。
        const uint8_t result = Wire.endTransmission();
        if (result == 0) {
            Serial.print("  device at 0x");
            Serial.println(address, HEX);
            found++;
        } else if (result == 5) {
            // en: A stuck bus differs from no ACK; stop this scan on timeout.
            // ja: バスが解放されない状態はACKなしとは別。timeoutなら走査を止める。
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
