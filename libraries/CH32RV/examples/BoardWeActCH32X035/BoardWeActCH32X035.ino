// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。
/* board: CH32X035:pnum=CH32X035F8U6 */

#include <CH32RVUsbDevice.h>
#include <CH32RVUsbPd.h>
#include <SPI.h>
// en: Optional I2C replaces USB or debug pins; choose one pair (see README).
// ja: 任意のI2CはUSBまたはデバッグ端子と排他。一組だけ選ぶ（README参照）。
// #include <Wire.h>

static const int LED_PIN = PB12;

static const int UART_TX = PA2;
static const int UART_RX = PA3;
static const int UART2_TX = PB0;
static const int UART2_RX = PB1;

static const int SPI_SCK = PA5;
static const int SPI_MISO = PA6;
static const int SPI_MOSI = PA7;
static const int SPI_CS = PA4;

static const int ANALOG_1 = A0;
static const int ANALOG_2 = A1;

static const int PWM_PIN = PB11;

// en: Debug-pin route; replaces SWD/DMSeq. / ja: デバッグ端子の経路。SWD/DMSeqと排他。
// static const int I2C_SDA = PC18;
// static const int I2C_SCL = PC19;
// en: USB-pin route; replaces USB data. / ja: USB端子の経路。USBデータと排他。
// static const int I2C_SDA = PC17;
// static const int I2C_SCL = PC16;

static uint8_t pwmLevel;
static int8_t pwmStep = 4;

void setup()
{
    pinMode(LED_PIN, OUTPUT);

    CH32RVUsbDeviceConfig usb;
    usb.product = "WeAct CH32X035";
    // en: Configure USB identity before starting the predefined CDC device.
    // ja: 定義済みCDCデバイスの起動前にUSBの識別情報を設定する。
    USBDevice.begin(usb);
    SerialUSB.begin();

    // en: Start attach detection without waiting for a PD source. / ja: PD電源の接続を待たずに検出を始める。
    USBPD.begin();

    Serial.begin(115200, SERIAL_8N1, UART_RX, UART_TX);
    Serial1.begin(115200, SERIAL_8N1, UART2_RX, UART2_TX);

    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, -1);
    pinMode(SPI_CS, OUTPUT);
    digitalWrite(SPI_CS, HIGH);

    // Wire.begin(I2C_SDA, I2C_SCL);

    pinMode(ANALOG_1, INPUT);
    pinMode(ANALOG_2, INPUT);
    pinMode(PWM_PIN, OUTPUT);
}

static void status(Print &out)
{
    out.print("A0=");
    out.print(analogRead(ANALOG_1));
    out.print(" A1=");
    out.print(analogRead(ANALOG_2));
    out.print(" PWM=");
    out.print(pwmLevel);
    out.print(" PD=");
    if (USBPD.ready()) {
        out.print(USBPD.voltage());
        out.print("mV/");
        out.print(USBPD.current());
        out.print("mA");
    } else {
        out.print(USBPD.connected() ? "no-PD" : "none");
    }
    out.println();
}

void loop()
{
    USBPD.maintain();

    static uint32_t lastSpi;
    if (millis() - lastSpi >= 100) {
        lastSpi = millis();
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

    while (Serial.available()) {
        SerialUSB.write(Serial.read());
    }
    while (Serial1.available()) {
        SerialUSB.write(Serial1.read());
    }
    while (SerialUSB.available()) {
        const int c = SerialUSB.read();
        Serial.write(c);
        Serial1.write(c);
    }

    static uint32_t lastStatus;
    if (millis() - lastStatus >= 1000) {
        lastStatus = millis();
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        status(Serial);
        if (SerialUSB.connected()) {
            status(SerialUSB);
        }
    }
}
