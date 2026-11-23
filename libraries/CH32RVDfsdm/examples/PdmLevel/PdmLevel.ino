/* requires: CH32RV_DFSDM_PADS, PE9, PD6 */
/*
 * PdmLevel
 *
 * en: A PDM microphone's level: CKOUT (PE9) clocks it at about 2 MHz, its data
 *     comes on DATIN1 (PD6), the DFSDM's sinc3 filter / 64 makes 31.25 kHz
 *     samples, and every 100 ms the sketch prints the peak and the RMS of them
 *     as a bar.
 * ja: PDM マイクの音量。CKOUT（PE9）で約 2 MHz を与え、データを DATIN1（PD6）で受け、
 *     DFSDM の sinc3・1/64 で 31.25 kHz のサンプルにし、100 ms ごとにピークと RMS を
 *     棒で表示します。
 */
#include <CH32RVDfsdm.h>
#include <math.h>

CH32RVDfsdm mic;

void setup()
{
    Serial.begin(115200);
    delay(500);
    if (!mic.begin(PE9, PD6, 2000000, 64, 3)) {
        Serial.println("begin() failed: not the DFSDM's pads");
        while (true) {
            delay(1000);
        }
    }
    Serial.printf("clock %lu Hz, %lu samples/s, full scale %ld\r\n", (unsigned long)mic.clock(),
                  (unsigned long)mic.sampleRate(), (long)mic.fullScale());
}

void loop()
{
    static int64_t sum;
    static double squares;
    static uint32_t count;
    static int32_t lo = INT32_MAX, hi = INT32_MIN;
    static uint32_t last;
    int32_t buf[32];
    const size_t n = mic.read(buf, 32);
    for (size_t i = 0; i < n; i++) {
        sum += buf[i];
        squares += (double)buf[i] * buf[i];
        lo = buf[i] < lo ? buf[i] : lo;
        hi = buf[i] > hi ? buf[i] : hi;
        count++;
    }
    if (millis() - last >= 100 && count) {
        last = millis();
        const double mean = (double)sum / count;
        const double rms = sqrt(fmax(squares / count - mean * mean, 0.0));   /* without the DC offset */
        const int32_t peak = (hi - lo) / 2;
        const int bar = (int)(40.0 * 8 * rms / mic.fullScale());   /* x8: speech is far under full scale */
        Serial.printf("rms %7ld peak %7ld |", (long)rms, (long)peak);
        for (int i = 0; i < 40; i++) {
            Serial.print(i < bar ? '#' : ' ');
        }
        Serial.println('|');
        sum = 0;
        squares = 0;
        count = 0;
        lo = INT32_MAX;
        hi = INT32_MIN;
        if (mic.overruns()) {
            Serial.printf("%lu samples lost so far\r\n", (unsigned long)mic.overruns());
        }
    }
}
