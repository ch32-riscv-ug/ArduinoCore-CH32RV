// en: Light WS2812 / SK6812 RGB LEDs on any pin: rgbLedWrite() for one LED, ch32rv_rgb_led_write() for a chain.
// ja: どの端子でもWS2812 / SK6812のRGB LEDを光らせる。1個はrgbLedWrite()、チェーンはch32rv_rgb_led_write()。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

static const uint8_t LED_PIN = PA1;
static const int CHAIN = 4;   // en: LEDs on the line / ja: 線につないだLEDの数

// en: The low time that makes the LEDs show what they received. WS2812 needs 50 us,
//     WS2812B-V5 and many newer parts 280 us; 300 covers both.
// ja: 受け取った色をLEDに表示させるLowの時間。WS2812は50 µs、WS2812B-V5など新しい部品は280 µs。両方に足りる300にする。
static const unsigned LATCH_US = 300;

static uint8_t grb[CHAIN * 3];   // en: the LEDs' bytes, in the order they go out / ja: 送る順に並べたLEDのバイト

static void showChain(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < CHAIN; i++) {
        // en: WS2812B takes green, red, blue. Dim the far ones so the order is visible.
        // ja: WS2812Bは緑・赤・青の順。遠いLEDほど暗くして、順番が見えるようにする。
        grb[3 * i] = g >> i;
        grb[3 * i + 1] = r >> i;
        grb[3 * i + 2] = b >> i;
    }
    // en: The whole chain in one frame (interrupts off for about 30 us per LED); then the latch.
    // ja: チェーン全体を1つのフレームで送る（割り込みはLED 1個につき約30 µs止まる）。そのあとラッチ。
    ch32rv_rgb_led_write(LED_PIN, grb, sizeof grb);
    delayMicroseconds(LATCH_US);
}

void setup()
{
}

void loop()
{
    // en: One LED alone: rgbLedWrite() (GRB; rgbLedWriteOrdered() takes another order).
    // ja: LED 1個だけなら rgbLedWrite()（GRB。ほかの順はrgbLedWriteOrdered()）。
    rgbLedWrite(LED_PIN, 0, 0, 0);
    delayMicroseconds(LATCH_US);
    showChain(64, 0, 0);
    delay(500);
    showChain(0, 64, 0);
    delay(500);
    showChain(0, 0, 64);
    delay(500);
    showChain(0, 0, 0);
    delay(500);
}
