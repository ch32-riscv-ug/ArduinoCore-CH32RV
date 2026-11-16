// en: Bridge a second UART made of two ordinary pins (SoftwareSerial) to the hardware Serial.
// ja: 2本の普通の端子で作ったUART（SoftwareSerial）とハードウェアのSerialをつなぐ。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: A1, A2 */
#include <SoftwareSerial.h>

// en: Any two pads work; RX takes the EXTI line of its bit number.
// ja: どの2本でもよい。RXはそのビット番号のEXTIの線を使う。
static const uint8_t SOFT_RX = A1;
static const uint8_t SOFT_TX = A2;

SoftwareSerial soft(SOFT_RX, SOFT_TX);

void setup()
{
    Serial.begin(115200);
    soft.begin(9600);                       // en: 8N1; begin(9600, SERIAL_8E1) for another format / ja: 形式は第2引数で
    Serial.println("SoftwareSerial bridge: what you type goes out on SOFT_TX");
}

void loop()
{
    // en: Sending holds interrupts for a whole frame, so what arrives meanwhile is lost (half duplex, as on AVR).
    // ja: 送信の1フレームの間は割り込みが止まるので、その間に来たものは失われる（AVRと同じ半二重）。
    while (Serial.available()) {
        soft.write(Serial.read());
    }
    while (soft.available()) {
        Serial.write(soft.read());
    }
    if (soft.overflow()) {
        Serial.println("\n[SoftwareSerial buffer overflow]");
    }
}
