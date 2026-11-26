// en: The smallest start for the WeAct CH32X035 Core Board: blink the LED and print over its USB-C port.
// ja: WeAct CH32X035 Core Boardのいちばん小さい出発点。LEDを点滅させ、USB-Cの口へ出力する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* board: CH32X035:part=CH32X035F8U6 */

#include <CH32RVUsbDevice.h>

// en: The on-board LED (High lights it).
// ja: 基板のLED（Highで点灯）。
static const int LED_PIN = PB12;

void setup()
{
    pinMode(LED_PIN, OUTPUT);

    // en: SerialUSB is a USB serial port on the board's USB-C. It also carries the DFU runtime, so the
    // en: next upload can pick this sketch's ch32rv-app:// port and needs no DOWNLOAD key.
    // ja: SerialUSBは基板のUSB-CのUSBシリアル。DFUランタイムも一緒に載るので、次の書き込みはこのスケッチの
    // ja: ch32rv-app://の口を選べばよく、DOWNLOADキーは要らない。
    SerialUSB.begin();
}

void loop()
{
    static unsigned long count;
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(900);

    // en: Nothing is lost while no terminal is open: the line just is not sent.
    // ja: 端末を開いていない間は送らないだけで、スケッチは止まらない。
    SerialUSB.print("hello from WeAct CH32X035 ");
    SerialUSB.println(count++);
}
