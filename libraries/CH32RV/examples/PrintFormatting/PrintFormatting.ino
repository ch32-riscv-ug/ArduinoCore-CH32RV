// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。
/* requires: flash=32K */

#include <avr/dtostrf.h>
void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    while (!Serial) {
    }

    Serial.print("255 -> ");
    Serial.print(255, DEC);
    Serial.print(' ');
    Serial.print(255, HEX);
    Serial.print(' ');
    Serial.print(255, OCT);
    Serial.print(' ');
    Serial.println(255, BIN);

    Serial.print("pi -> ");
    Serial.print(3.14159, 0);
    Serial.print(' ');
    Serial.print(3.14159, 2);
    Serial.print(' ');
    Serial.println(3.14159, 4);

    char buffer[16];
    // en: The destination must also hold the sign, decimal point and terminator. / ja: 保存先には符号・小数点・終端も収まる容量が必要。
    dtostrf(3.14159, 8, 3, buffer);
    Serial.print("dtostrf: [");
    Serial.print(buffer);
    Serial.println(']');

    String s = "count: ";
    s += 42;
    s += ", hex ";
    s += String(255, HEX);
    Serial.println(s);

    Serial.print("65 as text: ");
    Serial.println(65);
    Serial.print("65 as a byte: ");
    // en: write() sends a byte; print() formats the number as text. / ja: write()は生バイト、print()は数値を文字列にして送る。
    Serial.write(65);
    Serial.println();

    Serial.println();
    Serial.println("done");
}

void loop()
{
}
