// en: Read the ADC's internal channels: the reference, the supply voltage and the die temperature.
// ja: ADCの内部チャネル（基準電圧・電源電圧・チップの温度）を読む。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RV.h>
#include <math.h>

static const uint32_t PERIOD_MS = 2000;
static const int SAMPLES = 16;   // en: average a few readings: one is noisy / ja: 1回は揺れるので平均する

static uint32_t lastRun;

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    while (!Serial) {
    }
    Serial.print(CH32RV.getChipModel());
    Serial.println(": internal ADC channels");
}

void loop()
{
    if (millis() - lastRun < PERIOD_MS) {
        return;
    }
    lastRun = millis();

    // en: The reference, as analogRead() would return it at the current resolution; 0 where the chip has none.
    // ja: 基準電圧を、今の分解能でのanalogRead()と同じ単位で返す。持たない系列では0。
    long vref = 0;
    for (int i = 0; i < SAMPLES; i++) {
        vref += analogReadVref();
    }
    vref /= SAMPLES;

    // en: The supply in mV, worked out from the reference; 0 where unknown.
    // ja: 基準電圧から求めた電源電圧（mV）。わからない系列では0。
    const uint32_t vdd = CH32RV.getVddMillivolts();

    // en: Degrees C from the datasheet's typical constants: NAN where the chip has no sensor.
    // ja: データシートの代表値で換算した摂氏。センサの無い系列ではNAN。
    float temp = 0;
    int got = 0;
    for (int i = 0; i < SAMPLES; i++) {
        const float t = analogReadTemp();
        if (!isnan(t)) {
            temp += t;
            got++;
        }
    }

    Serial.print("Vref reading ");
    if (vref) {
        Serial.print(vref);
    } else {
        Serial.print("(none)");
    }
    Serial.print("  VDD ");
    if (vdd) {
        Serial.print(vdd);
        Serial.print(" mV");
    } else {
        Serial.print("(unknown)");
    }
    Serial.print("  die ");
    if (got) {
        Serial.print(temp / got, 1);
        Serial.println(" C (typical constants, not calibrated)");
    } else {
        Serial.println("(no sensor)");
    }
}
