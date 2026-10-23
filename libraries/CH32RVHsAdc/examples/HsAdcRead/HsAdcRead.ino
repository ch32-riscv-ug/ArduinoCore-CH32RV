// en: Read individual high-speed ADC samples and print them on UART.
// ja: 高速ADCで単発変換し、値をUARTへ表示する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* board: CH32H417 */

#include <CH32RV.h>
#include <CH32RVHsAdc.h>

CH32RVHsAdc hsadc;

void setup()
{
    Serial.begin(115200);
    // en: This changes the shared VIO18 supply; verify every attached part first.
    // ja: VIO18共通電源を変更するので、接続部品すべての許容電圧を先に確認する。
    CH32RV.setVio18Millivolts(3300);
    // en: Choose an ADC input and requested samples per second; sampleRate() reports the actual selection.
    // ja: 入力端子と毎秒のsample数を指定する。実際に選ばれたrateはsampleRate()で読む。
    if (!hsadc.begin(PC0, 20000000)) {
        Serial.println("PC0 is no HSADC input");
    }
    Serial.printf("HSADC at %lu samples/s\r\n", (unsigned long)hsadc.sampleRate());
}

void loop()
{
    // en: Use single conversions only while continuous capture is stopped.
    // ja: 連続取り込みを停止した状態で単発変換する。
    const uint16_t v = hsadc.read();
    Serial.printf("PC0 %4u  %4lu mV\r\n", v, (unsigned long)v * CH32RV.getVddMillivolts() / 1023u);
    delay(1000);
}
