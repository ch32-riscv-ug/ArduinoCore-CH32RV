/* One burst of port E at the DMA's own speed (20 MS/s at F_CPU 100 MHz), and
 * the timing of a PWM captured with it: PE3 carries analogWrite() at a known
 * frequency, so its period in samples measures the sample rate.
 * Prints every 2 s. *
 * board: CH32H417 */
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
    /* In hundredths, without float printf (not in the default C runtime). */
    const uint32_t period100 = rises > 1 ? (last - first) * 100u / (rises - 1) : 0u;
    Serial.printf("burst at %lu samples/s; PE3: %lu rising edges, %lu.%02lu samples a period\r\n",
                  (unsigned long)rate, (unsigned long)rises,
                  (unsigned long)(period100 / 100u), (unsigned long)(period100 % 100u));
    delay(2000);
}
