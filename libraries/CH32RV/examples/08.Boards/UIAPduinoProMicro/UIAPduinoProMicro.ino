// en: Blink the UIAPduino Pro Micro's LED by its printed pin number, with USB alone.
// ja: UIAPduino Pro MicroのLEDを、基板に印刷された番号で点滅させる（USBだけで動く）。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* board: UIAPDUINO_V003_V14 */

// en: The board's USB is the bootloader's (it uploads over HID). While the sketch runs, the USB does
// en: nothing: CH32V003 has no USB peripheral. So with USB alone, the LED is what you can see.
// ja: 基板のUSBはブートローダーのもの（HIDで書き込む）。スケッチが動いている間、USBは何もしない
// ja: （CH32V003にUSBの周辺機能は無い）。USBだけで目に見えるのはLEDだけ。
//
// en: To see Serial, wire a USB-UART adapter to TX = 15 (PD5) and RX = 16 (PD6), and call
// en: Serial.begin(115200). With a WCH-LinkE on SWIO (11 = PD1), SerialSDI or SerialDMSeq needs no wires.
// ja: Serialを見るには、USB-UARTの変換器をTX = 15（PD5）とRX = 16（PD6）につなぎ、
// ja: Serial.begin(115200)を呼ぶ。WCH-LinkEをSWIO（11 = PD1）につなげば、SerialSDIかSerialDMSeqで配線なしに読める。

// en: The printed numbers reach the pads: 2 is D2 is PC0, the on-board LED (High lights it).
// ja: 印刷の番号がそのまま端子になる: 2 = D2 = PC0 が基板のLED（Highで点灯）。
static const int LED_PIN = 2;

// en: Pads the board keeps for itself: 13 / 14 (PD3 / PD4) are the bootloader's USB, 11 (PD1) is SWIO,
// en: 17 (PD7) is RESET. Leave them alone so the board can still be uploaded to and recovered.
// ja: 基板が使う端子: 13 / 14（PD3 / PD4）はブートローダーのUSB、11（PD1）はSWIO、17（PD7）はRESET。
// ja: 書き込みと復旧ができるように、これらは使わない。

void setup()
{
    pinMode(LED_PIN, OUTPUT);
}

void loop()
{
    // en: Two short flashes, then a pause: a pattern no other sketch on the board shows by accident.
    // ja: 短く2回光ってから休む。ほかのスケッチがたまたま出す光り方と区別できる。
    for (int i = 0; i < 2; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(100);
        digitalWrite(LED_PIN, LOW);
        delay(150);
    }
    delay(700);
}
