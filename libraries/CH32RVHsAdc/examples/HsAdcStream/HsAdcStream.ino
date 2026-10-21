/* PC0 at 20 Msps, streamed to the host over USB: a vendor interface whose
 * bulk IN carries every sample (16-bit little-endian, 0..1023), 40 MB/s.
 * That needs the CH32H417's SuperSpeed port; on the full-speed one (about
 * 1 MB/s) most buffers are dropped and counted.
 *
 * Read it with libusb - several transfers queued at once, so the host never
 * leaves the endpoint idle (one synchronous read at a time drops buffers):
 *     VID:PID 1209:0001, the interface with class 0xFF, bulk IN.
 * Every second the sketch prints, on Serial: samples per second, buffers
 * sent, and buffers lost (HSADC overruns plus buffers the host was too slow
 * for).
 *
 * As in HsAdcRead, VIO18 is raised to 3.3 V for PC0's input range. *
 * board: CH32H417 */
#include <CH32RV.h>
#include <CH32RVHsAdc.h>
#include <CH32RVUsbDevice.h>

CH32RVHsAdc hsadc;
CH32RVUsbDevice device;
CH32RVUsbDeviceVendor stream(device);

static const uint16_t SAMPLES = 16384;                 /* 32 KB, 819 us at 20 Msps */
static uint16_t bufA[SAMPLES] __attribute__((aligned(16)));
static uint16_t bufB[SAMPLES] __attribute__((aligned(16)));
static uint32_t sent, busy;

void setup()
{
    Serial.begin(115200);
    CH32RV.setVio18Millivolts(3300);
    device.begin();
    hsadc.begin(PC0, 20000000);
    hsadc.startContinuous(bufA, bufB, SAMPLES);
}

void loop()
{
    if (const uint16_t *full = hsadc.readBuffer()) {
        /* The HSADC rewrites this buffer one buffer-time from now; the USB
         * transfer of it takes about 140 us at SuperSpeed. */
        if (stream.writeDirect(full, sizeof bufA)) {
            sent++;
        } else {
            busy++;
        }
    }
    static uint32_t last;
    if (millis() - last >= 1000) {
        last = millis();
        Serial.printf("%lu samples/s, sent %lu, lost %lu (overruns %lu, host busy %lu)\r\n",
                      (unsigned long)hsadc.sampleRate(), (unsigned long)sent,
                      (unsigned long)(hsadc.overruns() + busy), (unsigned long)hsadc.overruns(),
                      (unsigned long)busy);
    }
}
