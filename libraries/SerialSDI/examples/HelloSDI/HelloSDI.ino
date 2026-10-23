// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <SerialSDI.h>

void setup()
{
    // en: The baud argument is compatibility-only; no UART clock is configured.
    // ja: baud引数は互換用で、UART clockは設定しない。
    SerialSDI.begin(115200);
    SerialSDI.println("hello from the debug module");
}

void loop()
{
    SerialSDI.print("uptime ");
    SerialSDI.print(millis() / 1000);
    SerialSDI.println(" s");
    delay(1000);
}
