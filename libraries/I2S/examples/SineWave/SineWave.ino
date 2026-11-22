// en: Play a 441 Hz sine on an I2S DAC (MAX98357A, PCM5102A, ...) with the I2S library.
// ja: I2S の DAC（MAX98357A、PCM5102A など）に 441 Hz の正弦波を I2S ライブラリで流す。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: CH32RV_I2S2_PADS */

#include <I2S.h>

// en: BCLK, WS (LRCK) and DOUT: pads of I2S2 (the variant's CH32RV_I2S2_PADS). Change them to your wiring.
// ja: BCLK・WS（LRCK）・DOUT は I2S2 の端子（variant の CH32RV_I2S2_PADS）。配線に合わせて変える。
#if defined(CH32RV_SERIES_CH32H416)
static const int PIN_BCLK = PD3, PIN_WS = PB4, PIN_DOUT = PC3;     // en: H416 bonds no PB12-PB15 / ja: H416 は PB12〜PB15 が無い
#else
static const int PIN_BCLK = PB13, PIN_WS = PB12, PIN_DOUT = PB15;
#endif

static const uint32_t RATE = 44100;          // en: samples a second / ja: 1 秒のサンプル数
static const int PERIOD = 100;               // en: samples a period: 44100 / 100 = 441 Hz / ja: 1 周期のサンプル数

I2SClass i2s;
static int16_t wave[PERIOD];
static bool running;

void setup()
{
    Serial.begin(115200);
    for (int i = 0; i < PERIOD; i++) {
        // en: a quarter of full scale: loud enough, and no clipping in the DAC
        // ja: フルスケールの 1/4: 十分な音量で、DAC で歪まない
        wave[i] = (int16_t)(8192.0f * sinf(2.0f * PI * i / PERIOD));
    }
    i2s.setPins(PIN_BCLK, PIN_WS, PIN_DOUT);
    if (!i2s.begin(I2S_MODE_STD, RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
        // en: 1 a rate or width the hardware lacks, 2 pins that are no I2S's, 3 its DMA taken, 4 no memory
        // ja: 1 ハードに無い速さ・幅、2 I2S の端子でない、3 DMA が使用中、4 メモリー不足
        Serial.printf("I2S begin() failed: error %d\n", i2s.lastError());
        return;
    }
    running = true;
    Serial.printf("I2S at %lu Hz (asked %lu)\n", (unsigned long)i2s.sampleRate(), (unsigned long)RATE);
}

void loop()
{
    if (!running) {
        return;
    }
    // en: one period as stereo frames (left, right); write() waits while the buffer is full
    // ja: 1 周期をステレオのフレーム（左、右）で。バッファーが一杯の間 write() は待つ
    int16_t frames[2 * PERIOD];
    for (int i = 0; i < PERIOD; i++) {
        frames[2 * i] = frames[2 * i + 1] = wave[i];
    }
    i2s.write((const uint8_t *)frames, sizeof(frames));

    static uint32_t last;
    if (millis() - last >= 5000) {
        last = millis();
        // en: halves of the buffer that ran dry: more than 0 means loop() did something too slow
        // ja: 空になったバッファーの半分の数。0 より多ければ loop() で遅い処理をしている
        Serial.printf("underruns %lu\n", (unsigned long)i2s.underruns());
    }
}
