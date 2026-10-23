// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。
/* board: CH32H417 */

#include <CH32RVLogicCapture.h>

CH32RVLogicCapture logic;
static uint16_t buf[32768];

void setup()
{
    Serial.begin(115200);
    analogWriteFrequency(PE3, 100000);
    analogWrite(PE3, 128);
    logic.begin(PORT_E, 10000000);
}

void loop()
{
    // en: Burst capture blocks interrupts; its rate follows DMA, not the continuous timer.
    // ja: burst中は割り込みが止まる。連続取得のタイマーではなくDMAで間隔が決まる。
    const uint32_t rate = logic.captureBurst(buf, 32768);
    uint32_t rises = 0, first = 0, last = 0;
    for (uint32_t i = 1; i < 32768; i++) {
        if ((buf[i] & (1u << 3)) && !(buf[i - 1] & (1u << 3))) {
            if (!rises) {
                first = i;
            }
            last = i;
            rises++;
        }
    }

    // en: Compute hundredths without floating-point formatting. / ja: 浮動小数点の整形を使わず、百分の一単位で計算する。
    const uint32_t period100 = rises > 1 ? (last - first) * 100u / (rises - 1) : 0u;
    Serial.printf("burst at %lu samples/s; PE3: %lu rising edges, %lu.%02lu samples a period\r\n",
                  (unsigned long)rate, (unsigned long)rises,
                  (unsigned long)(period100 / 100u), (unsigned long)(period100 % 100u));
    delay(2000);
}
