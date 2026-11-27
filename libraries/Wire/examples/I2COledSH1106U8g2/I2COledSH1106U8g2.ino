// en: Text, a counter and a bar on an SH1106 128x64 OLED with the U8g2 library (fonts and drawing).
// ja: SH1106（128x64のOLED）に、U8g2のライブラリで文字・カウンタ・棒を描く（フォントと図形が使える）。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: I2C1 */
/* libraries: U8g2 (2.36.19) */

#include <Wire.h>
#include <U8g2lib.h>

// en: The pins: this chip's default I2C pads. The WeAct CH32X035 (F8U6) has I2C only on the debug pads
// en: PC18 / PC19 (or the USB pads PC16 / PC17): there use PC18 / PC19 and USE_DEBUG_PADS 1.
// ja: 端子。この石の既定のI2Cの端子。WeAct CH32X035（F8U6）ではI2Cがデバッグの端子PC18 / PC19（かUSBの端子
// ja: PC16 / PC17）にしか出ていないので、PC18 / PC19とUSE_DEBUG_PADS 1にする。
static const int SDA_PIN = SDA;
static const int SCL_PIN = SCL;
// static const int SDA_PIN = PC18;
// static const int SCL_PIN = PC19;

// en: 1 when the pins above are the debug interface's pads: the debugger lets go of them until the next reset
// en: (no WCH-Link or OEP probe can attach), and a USB device is kept up so the next sketch uploads over USB.
// ja: 上の端子がデバッグの端子のときは1。次のリセットまでデバッガーが端子を手放す（WCH-LinkやOEPのprobeから
// ja: 接続できない）。次のスケッチをUSBで書き込めるよう、USBのデバイスを動かしておく。
#define USE_DEBUG_PADS 0

#if USE_DEBUG_PADS
#include <CH32RV.h>
#include <CH32RVUsbDevice.h>
#endif

// en: Page mode (_1_): a 128-byte buffer, drawn one page at a time, so it fits 2 KB parts such as V003.
// en: With RAM to spare, U8G2_SH1106_128X64_NONAME_F_HW_I2C keeps the whole 1 KB screen and draws once.
// en: An SSD1306 module: U8G2_SSD1306_128X64_NONAME_1_HW_I2C.
// ja: ページの形（_1_）。128バイトのバッファで1ページずつ描くので、V003のようなRAM 2 KBの石にも入る。
// ja: RAMに余裕があれば U8G2_SH1106_128X64_NONAME_F_HW_I2C で画面全体（1 KB）を持ち、一度に描ける。
// ja: SSD1306のモジュールなら U8G2_SSD1306_128X64_NONAME_1_HW_I2C。
U8G2_SH1106_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

void setup()
{
#if USE_DEBUG_PADS
    SerialUSB.begin();
    CH32RV.releaseDebugPins();
#endif
    // en: U8g2 calls Wire.begin() with no pins on this core; Wire then opens the pins chosen here.
    // ja: このコアではU8g2が引数なしのWire.begin()を呼ぶ。Wireはここで選んだ端子で開く。
    Wire.setPins(SDA_PIN, SCL_PIN);
    // en: The module's address: 0x3C (U8g2 takes it shifted, 0x78); 0x3D with the jumper moved.
    // ja: モジュールのアドレス。0x3C（U8g2には1ビットずらした0x78で渡す）。ジャンパーを変えると0x3D。
    u8g2.setI2CAddress(0x3C << 1);
    u8g2.begin();
}

void loop()
{
    const unsigned long seconds = millis() / 1000;
    char line[22];
    snprintf(line, sizeof line, "uptime %lu s", seconds);
    u8g2.firstPage();
    do {
        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.drawStr(0, 10, "Hello, SH1106!");
        u8g2.drawStr(0, 24, "CH32RV + U8g2");
        u8g2.drawStr(0, 38, line);
        // en: A bar that fills in 10 seconds, then starts again.
        // ja: 10秒で満ちて、また始まる棒。
        u8g2.drawFrame(0, 48, 128, 12);
        u8g2.drawBox(2, 50, (unsigned)(seconds % 10 + 1) * 124 / 10, 8);
    } while (u8g2.nextPage());
    delay(500);
}
