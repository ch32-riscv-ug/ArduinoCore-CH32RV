/* BoardUIAPduinoProMicro - a starting point for the UIAPduino Pro Micro
 * CH32V003 V1.4: every peripheral the board brings out, set up with its pins
 * named. Delete what you do not need.
 *
 * The board: D0-D17 on PA1, PA2, PC0-PC7, PD0-PD7 (the variant's D<n>), the
 * LED on PC0 (D2), USB on PD3/PD4 (used by the USB bootloader; this core has
 * no USB in the application on V003 yet), SWIO on PD1 and RESET on PD7.
 *
 * What runs here: the UART, I2C, SPI, three analog inputs, a PWM output and
 * the LED; the status goes to Serial once a second. The V003 has one of each
 * UART, I2C and SPI.
 *
 * SPI: the board menu's SPI1 stays AUTO, and the pins are named here. The
 * maker's pinout shows NSS on PC1 (SPI's default route) or on PC0 (its
 * remap), but both are taken on this board - PC1 is I2C's SDA and PC0 the
 * LED - and Arduino drives chip select as a plain GPIO anyway, so CS is on
 * PC3. To use the pinout's place instead, pick one:
 *     SPI_CS = PC1  (then no I2C: comment Wire out)
 *     SPI_CS = PC0  (then no LED: comment LED_PIN out)
 *
 * board: UIAPDUINO_V003_V14
 */
#include <SPI.h>
#include <Wire.h>

static const int LED_PIN = PC0;           /* D2, high = on */

static const int UART_TX = PD5;           /* D15: USART1 */
static const int UART_RX = PD6;           /* D16 */

static const int I2C_SCL = PC2;           /* D4: I2C1 */
static const int I2C_SDA = PC1;           /* D3 */

static const int SPI_SCK = PC5;           /* D7: SPI1 */
static const int SPI_MISO = PC7;          /* D9 */
static const int SPI_MOSI = PC6;          /* D8 */
static const int SPI_CS = PC3;            /* D5, a GPIO (see above) */

static const int ANALOG_1 = A0;           /* PA2, D1 */
static const int ANALOG_2 = A1;           /* PA1, D0 */
static const int ANALOG_3 = A2;           /* PC4, D6 */

static const int PWM_PIN = PD2;           /* D12: TIM1 CH1 (also A3) */

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

        /* A byte through SPI (wire MOSI to MISO and it comes back). */
        digitalWrite(SPI_CS, LOW);
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

    /* Echo what the UART receives. */
    while (Serial.available()) {
        Serial.write(Serial.read());
    }

    static uint32_t lastStatus;
    if (millis() - lastStatus >= 1000) {
        lastStatus = millis();
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));

        /* Which I2C addresses answer. */
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
