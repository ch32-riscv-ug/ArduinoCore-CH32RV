// en: ppvision's CH32X035 board (CH32X035G8U6, a 0.96" 160x80 ST7735 LCD, two buttons, four LEDs): the LCD drawn
// en: with Adafruit's ST7735 library, the buttons and the LEDs.
// ja: ppvisionのCH32X035の基板（CH32X035G8U6、0.96インチ160x80のST7735の液晶、ボタン2つ、LED 4つ）。液晶を
// ja: AdafruitのST7735のライブラリで描き、ボタンとLEDを使う。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* board: CH32X035:part=CH32X035G8U6 */
/* libraries: Adafruit ST7735 and ST7789 Library (1.12.0), Adafruit GFX Library (2.0.0), Adafruit BusIO (1.17.4) */

#include <CH32RVUsbDevice.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

// en: The board has no schematic; these come from its sample sources (github.com/ppvision/CH32X035).
// ja: 回路図は公開されていない。以下は基板のサンプルのソース（github.com/ppvision/CH32X035）から。
// en: LCD on SPI1 (SCK PA5, MOSI PA7, the core's default pins), no backlight control.
// ja: 液晶はSPI1（SCK PA5、MOSI PA7。コアの既定の端子）。バックライトの制御は無い。
static const int LCD_CS = PB4;
static const int LCD_DC = PB1;
static const int LCD_RST = PB0;
// en: Buttons to GND, read with the pull-ups on: pressed = LOW.
// ja: ボタンはGNDへ。プルアップで読む。押すとLOW。
static const int BUTTON_UP = PB8;
static const int BUTTON_DOWN = PB6;
static const int LEDS[] = {PA0, PA1, PC0, PC3};
static const int LED_COUNT = sizeof LEDS / sizeof LEDS[0];

Adafruit_ST7735 tft(LCD_CS, LCD_DC, LCD_RST);

static int count = 0;

static void drawCount()
{
    // en: Text with a background colour overwrites the old digits; no clearing, no flicker.
    // ja: 背景色付きの文字で前の数字を上書きする。消さないのでちらつかない。
    tft.setTextSize(3);
    tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
    tft.setCursor(8, 30);
    char text[8];
    snprintf(text, sizeof text, "%5d", count);
    tft.print(text);
}

static void drawButtons(bool up, bool down)
{
    tft.fillRect(110, 30, 44, 16, up ? ST77XX_GREEN : ST77XX_BLACK);
    tft.drawRect(110, 30, 44, 16, ST77XX_GREEN);
    tft.fillRect(110, 50, 44, 16, down ? ST77XX_RED : ST77XX_BLACK);
    tft.drawRect(110, 50, 44, 16, ST77XX_RED);
}

void setup()
{
    // en: Messages on the board's USB serial port (and an upload over USB next time).
    // ja: 基板のUSBのシリアルポートにメッセージを出す（次回はUSBからも書き込める）。
    SerialUSB.begin();
    for (int i = 0; i < LED_COUNT; i++) {
        pinMode(LEDS[i], OUTPUT);
    }
    pinMode(BUTTON_UP, INPUT_PULLUP);
    pinMode(BUTTON_DOWN, INPUT_PULLUP);

    // en: The "plugin" 160x80 panel: offsets 26 / 1, BGR and inverted (INVON), all as the library sets it (checked
    // en: on the board: black is black).
    // ja: 160x80の「plugin」のパネル。オフセット26 / 1、BGR、反転（INVON）で、どれもライブラリの設定のまま（実機で
    // ja: 黒が黒に出ることを確認）。
    tft.initR(INITR_MINI160x80_PLUGIN);
    // en: Landscape, the right way up (checked on the board); 1 is upside down.
    // ja: 横向き。正しい向き（実機で確認）。1だと上下が逆。
    tft.setRotation(3);
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(4, 4);
    tft.print("CH32X035 + ST7735");
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(4, 68);
    tft.print("UP +1  DOWN -1");
    drawCount();
    drawButtons(false, false);
}

void loop()
{
    static bool lastUp = false;
    static bool lastDown = false;
    static unsigned long ledAt = 0;
    static int led = 0;

    const bool up = digitalRead(BUTTON_UP) == LOW;
    const bool down = digitalRead(BUTTON_DOWN) == LOW;
    if (up != lastUp || down != lastDown) {
        // en: Count on the press, not the release.
        // ja: 離したときでなく、押したときに数える。
        if (up && !lastUp) {
            count++;
        }
        if (down && !lastDown) {
            count--;
        }
        lastUp = up;
        lastDown = down;
        drawCount();
        drawButtons(up, down);
        SerialUSB.print("up ");
        SerialUSB.print(up ? "pressed" : "released");
        SerialUSB.print(", down ");
        SerialUSB.print(down ? "pressed" : "released");
        SerialUSB.print(", count ");
        SerialUSB.println(count);
        // en: Crude debounce: ignore the contacts' bounce for 20 ms.
        // ja: 簡単なチャタリング除け。接点の跳ねを20 ms見ない。
        delay(20);
    }

    // en: The four LEDs light one after another, one step every 200 ms.
    // ja: 4つのLEDが200 msごとに順に光る。
    if (millis() - ledAt >= 200) {
        ledAt = millis();
        for (int i = 0; i < LED_COUNT; i++) {
            digitalWrite(LEDS[i], i == led ? HIGH : LOW);
        }
        led = (led + 1) % LED_COUNT;
    }
}
