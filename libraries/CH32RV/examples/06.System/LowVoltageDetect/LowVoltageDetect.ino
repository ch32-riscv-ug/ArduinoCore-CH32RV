// en: Warn when the supply falls below a threshold (the PVD on its EXTI line), and raise the line from software.
// ja: 電源が閾値を下回ったら知らせる（PVDとそのEXTIの線）。線をソフトウェアから起こしてもみる。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RV.h>

static volatile uint32_t lowCount;

// en: Runs in the PVD's interrupt: only count, and print in loop().
// ja: PVDの割込みの中で呼ばれる。数えるだけにし、表示はloop()でする。
static void onLow()
{
    lowCount++;
}

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    // en: The levels are the datasheet's typical falling-edge thresholds; 0 past the last.
    // ja: 段はデータシートの下がるときの代表値。最後の次は0。
    Serial.print("PVD levels (mV):");
    for (uint8_t i = 0; CH32RV.lowVoltageLevel(i) != 0; i++) {
        Serial.print(' ');
        Serial.print(CH32RV.lowVoltageLevel(i));
    }
    Serial.println();
    // en: The lowest threshold at or above 3000 mV (the highest if none is): 0 where the chip has no PVD interrupt.
    // ja: 3000 mV以上で最も低い閾値（無ければ最も高いもの）。PVDの割込みが無いチップでは0。
    const uint32_t mv = CH32RV.setLowVoltageDetect(3000, onLow);
    if (mv == 0) {
        Serial.println("No PVD interrupt on this chip");
        return;
    }
    Serial.print("Threshold ");
    Serial.print(mv);
    Serial.print(" mV, VDD ");
    Serial.print(CH32RV.getVddMillivolts());
    Serial.println(" mV");
    Serial.println("t: raise the PVD line from software");
}

void loop()
{
    static uint32_t shown;
    const uint32_t n = lowCount;
    if (n != shown) {
        shown = n;
        Serial.print("Low voltage #");
        Serial.print(n);
        Serial.println(CH32RV.isLowVoltage() ? " (still low)" : " (back up)");
    }
#ifdef CH32RV_EXTI_PVD
    // en: The line's interrupt from software, as a fall below the threshold would raise it.
    // ja: 線の割込みをソフトウェアから。閾値を下回ったときと同じ道で呼ばれる。
    if (Serial.available() && Serial.read() == 't') {
        CH32RV.triggerExtiLine(CH32RV_EXTI_PVD);
    }
#endif
}
