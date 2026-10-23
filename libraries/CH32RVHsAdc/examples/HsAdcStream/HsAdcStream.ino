// en: Stream continuous high-speed ADC samples through USB bulk IN.
// ja: 高速ADCの連続データをUSBのbulk INへ送る。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* board: CH32H417 */

#include <CH32RV.h>
#include <CH32RVHsAdc.h>
#include <CH32RVUsbDevice.h>

CH32RVHsAdc hsadc;
CH32RVUsbDevice device;
CH32RVUsbDeviceVendor stream(device);

static const uint16_t SAMPLES = 16384;
static uint16_t bufA[SAMPLES] __attribute__((aligned(16)));
static uint16_t bufB[SAMPLES] __attribute__((aligned(16)));
static uint32_t sent, busy;

void setup()
{
    Serial.begin(115200);
    // en: This changes the shared VIO18 supply; verify every attached part first.
    // ja: VIO18共通電源を変更するので、接続部品すべての許容電圧を先に確認する。
    CH32RV.setVio18Millivolts(3300);
    device.begin();
    hsadc.begin(PC0, 20000000);
    // en: DMA hardware fills the two arrays alternately, without calling analogRead() for every sample.
    // ja: DMAが二つの配列を交互に埋める。CPUで毎回analogRead()する方式ではない。
    hsadc.startContinuous(bufA, bufB, SAMPLES);
}

void loop()
{
    // en: A non-null pointer means a completed buffer is available; no new buffer means no work this round.
    // ja: ポインタがあれば完成bufferを処理する。まだなければ今回の巡回では何もしない。
    if (const uint16_t *full = hsadc.readBuffer()) {
        // en: USB must finish reading before ADC DMA reuses this buffer; success only starts the transfer.
        // ja: 成功は転送開始の受付。ADCのDMAが再利用する前にUSBが読み終わる必要がある。
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
