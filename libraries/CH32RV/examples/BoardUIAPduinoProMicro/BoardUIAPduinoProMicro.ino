// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。
/* board: UIAPDUINO_V003_V14 */

#include <SPI.h>
#include <Wire.h>

static const int LED_PIN = PC0;

static const int UART_TX = PD5;
static const int UART_RX = PD6;

static const int I2C_SCL = PC2;
static const int I2C_SDA = PC1;

static const int SPI_SCK = PC5;
static const int SPI_MISO = PC7;
static const int SPI_MOSI = PC6;
// en: Use a GPIO CS to leave I2C and the LED pins available. / ja: I2CとLEDの端子を空けるため、CSは別のGPIOを使う。
static const int SPI_CS = PC3;

static const int ANALOG_1 = A0;
static const int ANALOG_2 = A1;
static const int ANALOG_3 = A2;

static const int PWM_PIN = PD2;

static uint8_t pwmLevel;
static int8_t pwmStep = 4;

void setup()
{
    pinMode(LED_PIN, OUTPUT);

    Serial.begin(115200, SERIAL_8N1, UART_RX, UART_TX);

    Wire.begin(I2C_SDA, I2C_SCL);

    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, -1);
    pinMode(SPI_CS, OUTPUT);
    digitalWrite(SPI_CS, HIGH);

    pinMode(ANALOG_1, INPUT);
    pinMode(ANALOG_2, INPUT);
    pinMode(ANALOG_3, INPUT);
    pinMode(PWM_PIN, OUTPUT);
}

void loop()
{
    static uint32_t lastTick;
    if (millis() - lastTick >= 100) {
        lastTick = millis();

        digitalWrite(SPI_CS, LOW);
        // en: Keep the selected device active for the whole SPI transaction.
        // ja: SPIのtransaction中は対象機器のCSを有効に保つ。
        SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
        SPI.transfer(0xA5);
        SPI.endTransaction();
        digitalWrite(SPI_CS, HIGH);

        pwmLevel = (uint8_t)(pwmLevel + pwmStep);
        if (pwmLevel == 0 || pwmLevel >= 252) {
            pwmStep = (int8_t)-pwmStep;
        }
        analogWrite(PWM_PIN, pwmLevel);
    }

    while (Serial.available()) {
        Serial.write(Serial.read());
    }

    static uint32_t lastStatus;
    if (millis() - lastStatus >= 1000) {
        lastStatus = millis();
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));

        Serial.print("I2C:");
        for (uint8_t address = 0x08; address < 0x78; address++) {
            Wire.beginTransmission(address);
            if (Wire.endTransmission() == 0) {
                Serial.print(' ');
                Serial.print(address, HEX);
            }
        }
        Serial.print(" A0=");
        Serial.print(analogRead(ANALOG_1));
        Serial.print(" A1=");
        Serial.print(analogRead(ANALOG_2));
        Serial.print(" A2=");
        Serial.print(analogRead(ANALOG_3));
        Serial.print(" PWM=");
        Serial.println(pwmLevel);
    }
}
