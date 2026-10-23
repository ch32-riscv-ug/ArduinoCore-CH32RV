// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <SerialSDI.h>
#include <stdio.h>

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    SerialSDI.begin(115200);

    printf("this line goes to the UART\n");

    // en: Redirect stdio only; explicit Serial calls still use UART. / ja: stdioだけを切り替える。Serialの明示呼び出しはUARTのまま。
    ch32rv_set_stdout(&SerialSDI);
    printf("and this one goes to the debug probe\n");

    // en: Restore UART before the periodic printf() output. / ja: 周期的なprintf()の前にUARTへ戻す。
    ch32rv_set_stdout(&Serial);
    printf("back on the UART\n");
}

void loop()
{
    static uint32_t n;
    printf("tick %lu\n", (unsigned long)n++);
    delay(1000);
}
