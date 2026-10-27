// en: CH32RVSPISlave checked with no master and no wiring: the chip clocks its own slave pins from GPIO.
// ja: マスターも配線も無しで CH32RVSPISlave を確かめる。スレーブの端子を同じチップの GPIO で叩く。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: SPI1 */

#include <CH32RVSPISlave.h>

/* The SPI1 pins of the board's default route, and a chip select beside them.
 * Nothing may be connected to them: the sketch drives SCK, MOSI and CS. */
static const int SCK_PIN = CH32RV_SPI1_SCK;
static const int MISO_PIN = CH32RV_SPI1_MISO;
static const int MOSI_PIN = CH32RV_SPI1_MOSI;
#if defined(CH32RV_SERIES_CH32V003) || defined(CH32RV_SERIES_CH32V002) || defined(CH32RV_SERIES_CH32V004) || \
    defined(CH32RV_SERIES_CH32V005) || defined(CH32RV_SERIES_CH32V006) || defined(CH32RV_SERIES_CH32V007) || \
    defined(CH32RV_SERIES_CH32M007)
static const int CS_PIN = PC4;
#elif defined(CH32RV_SERIES_CH32H415) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H417)
static const int CS_PIN = PF4;
#else
static const int CS_PIN = PA4;
#endif

static const unsigned HALF_US = 5;      // half a clock period: 100 kHz
static const unsigned SELECT_US = 30;   // CS falling to the first edge, and between transfers

CH32RVSPISlave slave;
static uint8_t mode, order;
volatile uint32_t g_result;             // 0 running, 1 pass, 2 fail (for a probe without a console)
volatile uint32_t g_failed_step;
static uint32_t delivered;

/* The master's half, in software, for mode 0..3 and either bit order. */
static uint8_t clock_byte(uint8_t out)
{
    const bool cpol = mode & 2, cpha = mode & 1;
    uint8_t in = 0;
    for (int i = 0; i < 8; i++) {
        const int bit = order == MSBFIRST ? 7 - i : i;
        if (!cpha) {
            digitalWrite(MOSI_PIN, (out >> bit) & 1);
            delayMicroseconds(HALF_US);
            in |= (uint8_t)(digitalRead(MISO_PIN) << bit);
            digitalWrite(SCK_PIN, !cpol);       // leading edge: both sample
            delayMicroseconds(HALF_US);
            digitalWrite(SCK_PIN, cpol);
        } else {
            digitalWrite(SCK_PIN, !cpol);       // leading edge: both shift out
            digitalWrite(MOSI_PIN, (out >> bit) & 1);
            delayMicroseconds(HALF_US);
            in |= (uint8_t)(digitalRead(MISO_PIN) << bit);
            digitalWrite(SCK_PIN, cpol);        // trailing edge: both sample
            delayMicroseconds(HALF_US);
        }
    }
    return in;
}

static void master(const uint8_t *out, uint8_t *in, size_t n)
{
    digitalWrite(CS_PIN, LOW);
    delayMicroseconds(SELECT_US);
    for (size_t i = 0; i < n; i++) {
        in[i] = clock_byte(out[i]);
    }
    delayMicroseconds(HALF_US);
    digitalWrite(CS_PIN, HIGH);
    delayMicroseconds(SELECT_US);
}

static bool start(uint8_t m, BitOrder o)
{
    mode = m;
    order = o;
    if (!slave.begin(SCK_PIN, MISO_PIN, MOSI_PIN, CS_PIN, m, o)) {
        return false;
    }
    /* After begin(): the pads the slave reads are driven by this sketch. */
    digitalWrite(SCK_PIN, (m & 2) ? HIGH : LOW);
    pinMode(SCK_PIN, OUTPUT);
    pinMode(MOSI_PIN, OUTPUT);
    digitalWrite(CS_PIN, HIGH);
    pinMode(CS_PIN, OUTPUT);
    delayMicroseconds(SELECT_US);
    return true;
}

static bool check(int step, bool ok)
{
    Serial.print(ok ? "  ok   " : "  FAIL ");
    Serial.println(step);
    if (!ok && !g_failed_step) {
        g_failed_step = (uint32_t)step;
    }
    return ok;
}

static bool same(int step, const uint8_t *got, const uint8_t *want, size_t n)
{
    const bool ok = memcmp(got, want, n) == 0;
    if (!ok) {
        Serial.print("  step ");
        Serial.print(step);
        Serial.print(" got");
        for (size_t i = 0; i < n; i++) {
            Serial.print(' ');
            Serial.print(got[i], HEX);
        }
        Serial.print(", want");
        for (size_t i = 0; i < n; i++) {
            Serial.print(' ');
            Serial.print(want[i], HEX);
        }
        Serial.println();
    }
    return check(step, ok);
}

static void gotTransfer(const CH32RVSPISlave::Result &r, void *)
{
    if (r.length == 3 && r.rx[0] == 0x77) {
        delivered++;
    }
}

static bool one_mode(uint8_t m, BitOrder o)
{
    bool ok = true;
    const int base = m * 100 + (o == LSBFIRST ? 1000 : 0);
    ok &= check(base + 1, start(m, o));

    // 1: a queued transfer both ways
    const uint8_t stx[4] = {0xA5, 0x3C, 0x00, 0xFF};
    const uint8_t mtx[4] = {0x12, 0x80, 0x01, 0xEE};
    uint8_t srx[4] = {}, mrx[4] = {};
    ok &= check(base + 2, slave.queue(stx, srx, 4));
    master(mtx, mrx, 4);
    CH32RVSPISlave::Result r;
    ok &= check(base + 3, slave.poll(r) && r.length == 4 && r.rx == srx);
    ok &= same(base + 4, srx, mtx, 4);
    ok &= same(base + 5, mrx, stx, 4);

    // 2: nothing queued - the fill byte goes out, the transfer is counted as missed
    slave.setFill(0x5A);
    master(mtx, mrx, 2);
    const uint8_t want6[2] = {0x5A, 0x5A};
    ok &= same(base + 6, mrx, want6, 2) && check(base + 6, slave.missed() == 1 && !slave.available());

    // 3: the master goes on past the buffer - fill after tx, only `size` kept, length counts all
    const uint8_t stx2[2] = {0xC3, 0x96};
    uint8_t srx2[2] = {}, mrx2[4] = {};
    ok &= check(base + 7, slave.queue(stx2, srx2, 2));
    master(mtx, mrx2, 4);
    ok &= check(base + 8, slave.poll(r) && r.length == 4 && srx2[0] == 0x12 && srx2[1] == 0x80);
    const uint8_t want9[4] = {0xC3, 0x96, 0x5A, 0x5A};
    ok &= same(base + 9, mrx2, want9, 4);

    // 4: two queued, two transfers, each its own data; a CS pulse with no clock uses nothing up
    const uint8_t a[2] = {0x01, 0x02}, b[2] = {0x03, 0x04};
    uint8_t ra[2] = {}, rb[2] = {}, m1[2] = {}, m2[2] = {};
    ok &= check(base + 10, slave.queue(a, ra, 2) && slave.queue(b, rb, 2) && slave.numTransactionsInFlight() == 2);
    master(mtx, m1, 0);
    master(mtx, m1, 2);
    master(mtx + 2, m2, 2);
    ok &= check(base + 11, m1[0] == 0x01 && m1[1] == 0x02 && m2[0] == 0x03 && m2[1] == 0x04);
    ok &= check(base + 12, slave.available() == 2 && slave.poll(r) && r.rx == ra && slave.poll(r) && r.rx == rb);
    ok &= check(base + 13, ra[0] == 0x12 && ra[1] == 0x80 && rb[0] == 0x01 && rb[1] == 0xEE);
    ok &= check(base + 14, slave.overruns() == 0 && slave.missed() == 1);

    // 5: the callback, from deliverEvents()
    slave.onTransfer(gotTransfer);
    const uint8_t c[3] = {0x77, 0x78, 0x79};
    uint8_t rc[3] = {}, mc[3] = {};
    delivered = 0;
    ok &= check(base + 15, slave.queue(nullptr, rc, 3));
    master(c, mc, 3);
    deliverEvents();
    ok &= check(base + 16, delivered == 1 && !slave.available() && mc[0] == 0x5A);
    slave.onTransfer(nullptr);

    slave.end();
    pinMode(SCK_PIN, INPUT);
    pinMode(MOSI_PIN, INPUT);
    pinMode(CS_PIN, INPUT);
    return ok;
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    bool ok = true;
    for (uint8_t m = 0; m < 4; m++) {
        Serial.print("mode ");
        Serial.println(m);
        ok &= one_mode(m, MSBFIRST);
    }
    Serial.println("mode 0 LSBFIRST");
    ok &= one_mode(0, LSBFIRST);
    g_result = ok ? 1 : 2;
    Serial.print("SPISLAVE ");
    Serial.println(ok ? "PASS" : "FAIL");
}

void loop()
{
    static uint32_t last;
    if (millis() - last >= 1000) {
        last = millis();
        Serial.print("SPISLAVE ");
        Serial.print(g_result == 1 ? "PASS" : "FAIL");
        Serial.print(" first failed step ");
        Serial.println(g_failed_step);
    }
}
