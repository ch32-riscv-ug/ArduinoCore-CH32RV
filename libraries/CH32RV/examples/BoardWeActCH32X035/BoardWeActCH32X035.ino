/* BoardWeActCH32X035 - a starting point for the WeAct CH32X035 Core Board
 * (CH32X035F8U6, V1.0): every peripheral the board brings out, set up with
 * its pins named. Delete what you do not need; the commented-out parts are
 * the ones that cannot run together with the rest (see each).
 *
 * The board: USB-C (USB on PC16/PC17, PD's CC1/CC2 on PC14/PC15), a blue LED
 * on PB12 (high = on), the DOWNLOAD key (holds USB D+ high at reset: the
 * factory USB bootloader), the debug header PC18 (DIO) / PC19 (DCK), and on
 * the pin headers PA0-PA7, PB0, PB1, PB3, PB11, PB12, PC14, PC15.
 *
 * What runs here: USB (a serial port, SerialUSB, plus the DFU runtime that
 * lets you upload over USB again), USB PD (asks the charger for 5 V), two
 * UARTs, SPI, two analog inputs, a PWM output and the LED. The status goes to
 * SerialUSB and Serial once a second.
 *
 * Choices this board forces:
 *  - I2C exists only on the USB pins (PC16/PC17) or the debug pins
 *    (PC18/PC19). It is commented out below; taking it costs USB or the debug
 *    link (uploading through a probe still works - the probe resets the chip).
 *  - USART1 is not bonded on this package, and USART3's RX (PB4) is not
 *    either: the UARTs are USART2 (PA2/PA3) and USART4 (PB0/PB1).
 *  - PA0-PA3 are also TIM2's PWM pins and PA6/PA7 TIM3's; the PWM below uses
 *    PB11 (TIM1) so that the UARTs, SPI and analog inputs keep theirs.
 *
 * board: CH32X035:pnum=CH32X035F8U6
 */
#include <CH32RVUsbDevice.h>
#include <CH32RVUsbPd.h>
#include <SPI.h>
// #include <Wire.h>

/* ---- pins, as the board brings them out ---- */
static const int LED_PIN = PB12;          /* blue LED, high = on */

static const int UART_TX = PA2;           /* Serial: USART2 */
static const int UART_RX = PA3;
static const int UART2_TX = PB0;          /* Serial1: USART4 */
static const int UART2_RX = PB1;

static const int SPI_SCK = PA5;           /* SPI1 */
static const int SPI_MISO = PA6;
static const int SPI_MOSI = PA7;
static const int SPI_CS = PA4;            /* any GPIO; the library drives it */

static const int ANALOG_1 = A0;           /* PA0 */
static const int ANALOG_2 = A1;           /* PA1 */

static const int PWM_PIN = PB11;          /* TIM1 CH3 */

/* I2C: only one of these pairs, and only without what it replaces. */
// static const int I2C_SDA = PC18;       /* on the debug header: no SWD/DMSEQ */
// static const int I2C_SCL = PC19;
// static const int I2C_SDA = PC17;       /* on the USB pins: no USB */
// static const int I2C_SCL = PC16;

static uint8_t pwmLevel;
static int8_t pwmStep = 4;

void setup()
{
    pinMode(LED_PIN, OUTPUT);

    /* USB: SerialUSB is predefined; begin() starts the device (with the DFU
     * runtime, so the next upload can go over USB). Name the product here. */
    CH32RVUsbDeviceConfig usb;
    usb.product = "WeAct CH32X035";
    USBDevice.begin(usb);
    SerialUSB.begin();

    /* USB PD: ask a PD charger for 5 V. Without one (a PC's port), ready()
     * just stays false; nothing waits for it. */
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

    /* A byte through SPI (wire MOSI to MISO and it comes back). */
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

    /* What arrives on either UART goes out on SerialUSB, and back. */
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
