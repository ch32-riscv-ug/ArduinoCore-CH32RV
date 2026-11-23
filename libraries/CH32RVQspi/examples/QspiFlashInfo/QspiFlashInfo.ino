// en: Read a QSPI flash's JEDEC ID and its first bytes on one, two and four lines (CH32RVQspi).
// ja: QSPI のフラッシュの JEDEC ID と先頭のバイトを、1・2・4 線で読む（CH32RVQspi）。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: CH32RV_QSPI_PADS_ANY */

#include <CH32RVQspi.h>

// en: The QSPI1 pads of the first flash (the variant's CH32RV_QSPI1_PADS). Change them to your wiring.
// ja: QSPI1 の 1 つ目のフラッシュの端子（variant の CH32RV_QSPI1_PADS）。配線に合わせて変える。
#if defined(CH32RV_SERIES_CH32V205)
static const int PIN_SCK = PA12, PIN_CS = PB0, PIN_IO0 = PA2, PIN_IO1 = PA3, PIN_IO2 = PA0, PIN_IO3 = PA1;
CH32RVQspi qspi;
#elif defined(CH32RV_SERIES_CH32H416)
// en: H416 bonds QSPI2's pads only / ja: H416 は QSPI2 の端子だけ
static const int PIN_SCK = PF6, PIN_CS = PF7, PIN_IO0 = PF8, PIN_IO1 = PF9, PIN_IO2 = PF10, PIN_IO3 = PE15;
CH32RVQspi qspi(2);
#else
static const int PIN_SCK = PB2, PIN_CS = PB6, PIN_IO0 = PD11, PIN_IO1 = PD12, PIN_IO2 = PD14, PIN_IO3 = PD13;
CH32RVQspi qspi;
#endif

static void dump(const char *name, const uint8_t *b, size_t n)
{
    Serial.printf("%-22s", name);
    for (size_t i = 0; i < n; i++) {
        Serial.printf(" %02X", b[i]);
    }
    Serial.println();
}

void setup()
{
    Serial.begin(115200);
    if (!qspi.begin(PIN_SCK, PIN_CS, PIN_IO0, PIN_IO1, PIN_IO2, PIN_IO3)) {
        Serial.println("QSPI begin() failed: the pins are not one QSPI flash's");
        return;
    }
    Serial.printf("QSPI at %lu Hz\n", (unsigned long)qspi.setClock(10000000));

    // en: 9F: the JEDEC ID, three bytes on one line (manufacturer, type, capacity)
    // ja: 9F: JEDEC ID。1 線で 3 バイト（メーカー、種類、容量）
    CH32RVQspiCommand id;
    id.instruction = 0x9F;
    uint8_t jedec[3];
    if (qspi.read(id, jedec, sizeof jedec)) {
        dump("JEDEC ID", jedec, sizeof jedec);
    }

    uint8_t data[16];
    // en: 0B: fast read, the address on one line, 8 dummy clocks
    // ja: 0B: 高速読み出し。アドレスは 1 線、ダミー 8 クロック
    CH32RVQspiCommand fast;
    fast.instruction = 0x0B;
    fast.addressLines = 1;
    fast.dummyCycles = 8;
    if (qspi.read(fast, data, sizeof data)) {
        dump("0B fast read", data, sizeof data);
    }
    // en: BB: dual I/O, the address and the data on two lines, 4 dummy clocks
    // ja: BB: 2 線 I/O。アドレスとデータを 2 線、ダミー 4 クロック
    CH32RVQspiCommand dual = fast;
    dual.instruction = 0xBB;
    dual.addressLines = 2;
    dual.dummyCycles = 4;
    dual.dataLines = 2;
    if (qspi.read(dual, data, sizeof data)) {
        dump("BB dual I/O read", data, sizeof data);
    }
    // en: EB: quad I/O (the flash's QE bit has to be set; many parts ship with it), 6 dummy clocks
    // ja: EB: 4 線 I/O（フラッシュの QE ビットが要る。最初から立っている品も多い）、ダミー 6 クロック
    CH32RVQspiCommand quad = fast;
    quad.instruction = 0xEB;
    quad.addressLines = 4;
    quad.dummyCycles = 6;
    quad.dataLines = 4;
    if (qspi.read(quad, data, sizeof data)) {
        dump("EB quad I/O read", data, sizeof data);
    }
}

void loop()
{
}
