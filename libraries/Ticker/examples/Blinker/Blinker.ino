// en: Blink an LED from a Ticker, change its rate from another, and stop after a while - arduino-esp32's Ticker API.
// ja: TickerでLEDを点滅させ、別のTickerで速さを変え、しばらくで止める。arduino-esp32のTickerのAPI。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <Ticker.h>

static const uint8_t LED = PA1;

static Ticker blinker;
static Ticker toggler;
static Ticker changer;
static float blinkerPace = 0.1f;          // en: seconds / ja: 秒
static const float togglePeriod = 5.0f;   // en: seconds / ja: 秒

// en: Ticker callbacks run in a timer interrupt: keep them short, no Serial or delay() here.
// ja: Tickerのコールバックはタイマーの割り込みで動く。短くし、Serialやdelay()は使わない。
static void blink()
{
    digitalWrite(LED, !digitalRead(LED));
}

static void toggle()
{
    static bool isBlinking = false;
    if (isBlinking) {
        blinker.detach();
        isBlinking = false;
    } else {
        blinker.attach(blinkerPace, blink);
        isBlinking = true;
    }
    digitalWrite(LED, LOW);
}

static void change()
{
    blinkerPace = 0.5f;
}

void setup()
{
    pinMode(LED, OUTPUT);
    toggler.attach(togglePeriod, toggle);
    // en: once(): a single call, after 30 s.
    // ja: once(): 30秒後に1回だけ呼ぶ。
    changer.once(30, change);
}

void loop()
{
}
