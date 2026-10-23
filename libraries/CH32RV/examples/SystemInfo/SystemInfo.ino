// en: Print build, memory and optional chip-signature information.
// ja: ビルド対象・メモリ・利用可能なチップ固有情報を表示する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RV.h>

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    Serial.print("Series: ");
    Serial.println(CH32RV.getChipModel());
    Serial.print("CPU MHz: ");
    Serial.println(CH32RV.getCpuFreqMHz());
    Serial.print("Reset: ");
    Serial.println(CH32RV.resetReasonName());
    Serial.print("Sketch bytes: ");
    Serial.println(CH32RV.getSketchSize());
    // en: This is unused linked Flash, not an area reserved for persistent settings.
    // ja: リンク対象Flashの未使用量。設定保存用に予約された領域ではない。
    Serial.print("Unused sketch Flash bytes: ");
    Serial.println(CH32RV.getFreeSketchSpace());
    Serial.print("Heap bytes: ");
    Serial.println(CH32RV.getHeapSize());
    Serial.print("Free heap bytes: ");
    Serial.println(CH32RV.getFreeHeap());

    // en: Optional members exist only when the variant declares an electronic signature.
    // ja: 固有情報を持つvariantの場合だけ、対応するメンバーを呼び出す。
#ifdef CH32RV_ESIG_FLACAP_ADDR
    Serial.print("Silicon Flash bytes: ");
    Serial.println(CH32RV.getFlashChipSize());
    const uint64_t uid = CH32RV.getUniqueId();
    Serial.print("UID high/low (hex): ");
    Serial.print((uint32_t)(uid >> 32), HEX);
    Serial.print('/');
    Serial.println((uint32_t)uid, HEX);
#else
    Serial.println("Electronic signature unavailable");
#endif
    Serial.println("Send i to print free heap again");
}

void loop()
{
    if (Serial.available() && Serial.read() == 'i') {
        // en: Free space can be fragmented; this is not the largest allocatable block.
        // ja: 空き領域は分断されている場合があり、この大きさを一度に確保できるとは限らない。
        Serial.println(CH32RV.getFreeHeap());
    }
}
