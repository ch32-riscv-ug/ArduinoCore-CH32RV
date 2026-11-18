// en: Draw on an ST7789 240x240 display while the previous lines are still going out by DMA (transferAsync()).
// ja: 前の行をDMAで送っている間に次の行を描き、ST7789の240x240表示器に描画する(transferAsync())。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: SPI1, PA2, PA3, PA4, ram=10K */

#include <SPI.h>

// en: Change these to the GPIOs your display's CS, DC and RST are wired to.
// ja: 表示器のCS・DC・RSTをつないだGPIOに合わせて変える。
static const uint8_t CS_PIN = PA4;
static const uint8_t DC_PIN = PA3;
static const uint8_t RST_PIN = PA2;

static const uint16_t WIDTH = 240, HEIGHT = 240;
static const uint8_t LINES = 8;                    // lines per buffer
static uint16_t buffers[2][WIDTH * LINES];         // two buffers: one drawn while the other is sent

static void command(uint8_t c, const uint8_t *data = nullptr, size_t n = 0)
{
    digitalWrite(DC_PIN, LOW);
    digitalWrite(CS_PIN, LOW);
    SPI.transfer(c);
    digitalWrite(DC_PIN, HIGH);
    if (n) {
        SPI.writeBytes(data, n);
    }
    digitalWrite(CS_PIN, HIGH);
}

static void displayBegin()
{
    digitalWrite(RST_PIN, LOW);
    delay(10);
    digitalWrite(RST_PIN, HIGH);
    delay(120);
    SPI.beginTransaction(SPISettings(24000000, MSBFIRST, SPI_MODE0));
    command(0x01);                                 // software reset
    delay(150);
    command(0x11);                                 // sleep out
    delay(10);
    static const uint8_t colmod = 0x55;            // 16 bits a pixel
    command(0x3A, &colmod, 1);
    command(0x21);                                 // inversion on (most 240x240 panels)
    command(0x29);                                 // display on
    static const uint8_t columns[] = {0, 0, 0, WIDTH - 1}, rows[] = {0, 0, 0, HEIGHT - 1};
    command(0x2A, columns, sizeof columns);
    command(0x2B, rows, sizeof rows);
    SPI.endTransaction();
}

// en: The pixels of `count` lines from `y`, in the panel's byte order (high byte first).
// ja: yから`count`行の画素を、表示器のbyte順(上位byteが先)で作る。
static void drawLines(uint16_t *out, uint16_t y, uint32_t frame)
{
    for (uint8_t l = 0; l < LINES; l++) {
        for (uint16_t x = 0; x < WIDTH; x++) {
            const uint16_t r = (uint16_t)((x + frame) & 0x1F), g = (uint16_t)((y + l) & 0x3F), b = (uint16_t)((x ^ (y + l)) >> 3 & 0x1F);
            const uint16_t c = (uint16_t)(r << 11 | g << 5 | b);
            out[l * WIDTH + x] = (uint16_t)(c >> 8 | c << 8);
        }
    }
}

void setup()
{
    Serial.begin(115200);
    pinMode(CS_PIN, OUTPUT);
    pinMode(DC_PIN, OUTPUT);
    pinMode(RST_PIN, OUTPUT);
    digitalWrite(CS_PIN, HIGH);
    SPI.begin();
    displayBegin();
}

void loop()
{
    static uint32_t frame;
    const uint32_t start = millis();
    SPI.beginTransaction(SPISettings(24000000, MSBFIRST, SPI_MODE0));
    digitalWrite(DC_PIN, LOW);
    digitalWrite(CS_PIN, LOW);
    SPI.transfer(0x2C);                            // memory write: the pixels follow
    digitalWrite(DC_PIN, HIGH);
    uint8_t next = 0;
    bool sending = false;
    for (uint16_t y = 0; y < HEIGHT; y += LINES) {
        // en: Draw into one buffer while the other one is still being sent.
        // ja: もう一方のbufferを送っている間に、こちらのbufferに描く。
        drawLines(buffers[next], y, frame);
        while (sending && !SPI.finishedAsync()) {
        }
        if (!SPI.transferAsync(buffers[next], nullptr, sizeof buffers[next])) {
            // en: No DMA channel free (or SoftSPI): send it here and now.
            // ja: DMAのchannelが空いていない(またはSoftSPI)ときは、ここで送る。
            SPI.writeBytes((const uint8_t *)buffers[next], sizeof buffers[next]);
            sending = false;
        } else {
            sending = true;
        }
        next ^= 1;
    }
    while (sending && !SPI.finishedAsync()) {
    }
    digitalWrite(CS_PIN, HIGH);
    SPI.endTransaction();
    Serial.print("frame ms: ");
    Serial.println(millis() - start);
    frame++;
}
