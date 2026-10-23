// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。
/* board: CH32H417 */

#include <CH32RVLogicCapture.h>
#include <CH32RVUsbDevice.h>

CH32RVLogicCapture logic;
CH32RVUsbDevice device;
CH32RVUsbDeviceVendor stream(device);

static const uint32_t SAMPLES = 32768;
static uint16_t buf[SAMPLES] __attribute__((aligned(16)));
static uint32_t sent, busy;

void setup()
{
    Serial.begin(115200);
    analogWriteFrequency(PE3, 100000);
    analogWrite(PE3, 128);
    device.begin();
    logic.begin(PORT_E, CH32RVLogicCapture::maxContinuousRate());
    logic.startContinuous(buf, SAMPLES);
}

void loop()
{
    if (const uint16_t *half = logic.readBuffer()) {
        // en: Half the uint16_t samples occupy SAMPLES bytes; retain them until USB completes.
        // ja: uint16_t配列の半分はSAMPLES bytes。USB転送完了まで保持する。
        if (stream.writeDirect(half, SAMPLES)) {
            sent++;
        } else {
            busy++;
        }
    }
    static uint32_t last;
    if (millis() - last >= 1000) {
        last = millis();
        Serial.printf("%lu samples/s: sent %lu, lost %lu (overruns %lu, host busy %lu)\r\n",
                      (unsigned long)logic.sampleRate(), (unsigned long)sent,
                      (unsigned long)(logic.overruns() + busy), (unsigned long)logic.overruns(),
                      (unsigned long)busy);
    }
}
