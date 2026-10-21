/* Port E sampled continuously at 12.5 MS/s (F_CPU / 8) and streamed over USB
 * - 25 MB/s, the CH32H417's SuperSpeed port. Each half of the buffer goes
 * out as one bulk IN transfer of the vendor interface (class 0xFF), 16-bit
 * samples, bit n = PEn. PE3 carries a 100 kHz PWM to look at.
 *
 * Read with libusb, several async transfers queued. Serial prints the
 * halves sent and lost each second. *
 * board: CH32H417 */
#include <CH32RVLogicCapture.h>
#include <CH32RVUsbDevice.h>

CH32RVLogicCapture logic;
CH32RVUsbDevice device;
CH32RVUsbDeviceVendor stream(device);

static const uint32_t SAMPLES = 32768;                 /* halves of 16384 = 32 KB */
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
        if (stream.writeDirect(half, SAMPLES)) {      /* SAMPLES / 2 values = SAMPLES bytes */
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
