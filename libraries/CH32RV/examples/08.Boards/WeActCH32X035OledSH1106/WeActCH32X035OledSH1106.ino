// en: An SH1106 128x64 OLED on the WeAct CH32X035 Core Board's PC18 / PC19, drawn with the U8g2 library.
// ja: WeAct CH32X035 Core BoardのPC18 / PC19につないだSH1106（128x64のOLED）に、U8g2のライブラリで描く。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* board: CH32X035:part=CH32X035F8U6 */
/* libraries: U8g2 (2.36.19) */

#include <CH32RV.h>
#include <CH32RVUsbDevice.h>
#include <Wire.h>
#include <U8g2lib.h>

// en: On this board I2C is only on the debug pads PC18 / PC19 (or the USB pads PC16 / PC17).
// ja: この基板ではI2Cがデバッグの端子PC18 / PC19（かUSBの端子PC16 / PC17）にしか出ていない。
static const int SDA_PIN = PC18;
static const int SCL_PIN = PC19;

// en: Page mode: a 128-byte buffer, drawn one page at a time. U8G2_SH1106_128X64_NONAME_F_HW_I2C keeps the
// en: whole 1 KB screen instead (this chip has 20 KB of RAM).
// ja: ページの形。128バイトのバッファで1ページずつ描く。U8G2_SH1106_128X64_NONAME_F_HW_I2C なら画面全体（1 KB）を
// ja: 持つ（この石のRAMは20 KB）。
U8G2_SH1106_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

void setup()
{
    // en: The USB device first: with the debug pads given away, the board's USB (its ch32rv-app:// port or
    // en: serial port) is how the next sketch gets uploaded.
    // ja: 先にUSBのデバイス。デバッグの端子を手放すので、次のスケッチは基板のUSB（ch32rv-app://の口か
    // ja: シリアルポート）から書き込む。
    SerialUSB.begin();
    // en: PC18 / PC19 become ordinary pads until the next reset; no WCH-Link or OEP probe can attach meanwhile.
    // ja: 次のリセットまでPC18 / PC19が普通の端子になる。その間はWCH-LinkやOEPのprobeから接続できない。
    CH32RV.releaseDebugPins();
    // en: U8g2 calls Wire.begin() with no pins on this core; Wire then opens the pins chosen here.
    // ja: このコアではU8g2が引数なしのWire.begin()を呼ぶ。Wireはここで選んだ端子で開く。
    Wire.setPins(SDA_PIN, SCL_PIN);
    // en: The module's address: 0x3C (U8g2 takes it shifted); 0x3D with the module's jumper moved.
    // ja: モジュールのアドレス。0x3C（U8g2には1ビットずらして渡す）。モジュールのジャンパーを変えると0x3D。
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
        u8g2.drawStr(0, 24, "WeAct CH32X035");
        u8g2.drawStr(0, 38, line);
        // en: A bar that fills in 10 seconds, then starts again.
        // ja: 10秒で満ちて、また始まる棒。
        u8g2.drawFrame(0, 48, 128, 12);
        u8g2.drawBox(2, 50, (unsigned)(seconds % 10 + 1) * 124 / 10, 8);
    } while (u8g2.nextPage());
    delay(500);
}
