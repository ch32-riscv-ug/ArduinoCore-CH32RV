// en: Show eight colour bars on a 480 x 272 RGB panel (16-bit, RGB565) with the LTDC (CH32RVLtdc).
// ja: 480 x 272 の RGB パネル（16 ビット、RGB565）に 8 本の色の帯を LTDC で出す（CH32RVLtdc）。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: CH32RV_LTDC_PADS, PE13, PE12, PE11, PD15, PC7 */

#include <CH32RVLtdc.h>

CH32RVLtdc lcd;
static uint16_t *frame;

void setup()
{
    Serial.begin(115200);
    // en: the panel's timing (a common 4.3-inch 480 x 272 module; the EVT's)
    // ja: パネルのタイミング（よくある 4.3 インチ 480 x 272。EVT の値）
    CH32RVLtdcTiming t;
    t.width = 480;
    t.height = 272;
    t.hsync = 4;  t.hbp = 43; t.hfp = 8;
    t.vsync = 4;  t.vbp = 12; t.vfp = 8;
    t.pixelHz = 10000000;
    // en: CLK, HSYNC, VSYNC, DE, then the 16 colour lines of RGB565 (R3-R7, G2-G7, B3-B7); -1 unused
    // ja: CLK・HSYNC・VSYNC・DE と、RGB565 の 16 本（R3〜R7、G2〜G7、B3〜B7）。-1 は使わない
    // en: CH32H417's pads (the variant's CH32RV_LTDC_PADS lists the others; H415 / H416 bond fewer)
    // ja: CH32H417 の端子（ほかは variant の CH32RV_LTDC_PADS。H415 / H416 は少ない）
    const int r[8] = {-1, -1, -1, PB0, PA5, PC0, PB1, PD4};
    const int g[8] = {-1, -1, PD15, PE11, PB10, PB11, PC7, PB15};
    const int b[8] = {-1, -1, -1, PD7, PE12, PD5, PA14, PB9};
    if (!lcd.setSignalPins(PB14, PA6, PA4, PE13) || !lcd.setColorPins(r, g, b)) {
        Serial.println("a pin is not the LTDC's for that role (the variant's CH32RV_LTDC_PADS)");
        return;
    }
    frame = static_cast<uint16_t *>(malloc(480 * 272 * 2));   // en: 255 KB in the shared SRAM / ja: 共有 SRAM に 255 KB
    if (!frame || !lcd.begin(t)) {
        Serial.println("LTDC begin() failed");
        return;
    }
    static const uint16_t bars[8] = {0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0x0000};
    for (int y = 0; y < 272; y++) {
        for (int x = 0; x < 480; x++) {
            frame[y * 480 + x] = bars[x / 60];
        }
    }
    lcd.layer(1, frame, CH32RV_LTDC_RGB565, 0, 0, 480, 272);
    Serial.printf("LTDC at %lu Hz pixel clock\n", (unsigned long)lcd.pixelClock());
}

void loop()
{
}
