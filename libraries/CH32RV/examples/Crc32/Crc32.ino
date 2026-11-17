// en: Check a block of data with CH32RV.crc32(): the CRC block where the chip has one, the same result elsewhere.
// ja: CH32RV.crc32()でデータの塊を検査する。CRCのハードウェアがある石はそれで、無い石でも同じ結果になる。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RV.h>

static const char CHECK[] = "123456789";
static const uint32_t CHECK_CRC = 0x0376E6E7;   // en: CRC-32/MPEG-2's check value / ja: CRC-32/MPEG-2の検査値

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    while (!Serial) {
    }

    // en: One call over the whole buffer.
    // ja: バッファ全体を1回で。
    const uint32_t crc = CH32RV.crc32(CHECK, 9);
    Serial.print("crc32(\"123456789\") = 0x");
    Serial.print(crc, HEX);
    Serial.println(crc == CHECK_CRC ? " (as expected)" : " (WRONG)");

    // en: The same data in two pieces: pass the first result on as the starting value.
    // ja: 同じデータを2つに分けて。最初の結果を次の開始値として渡す。
    uint32_t part = CH32RV.crc32(CHECK, 4);
    part = CH32RV.crc32(CHECK + 4, 5, part);
    Serial.print("in two pieces    = 0x");
    Serial.println(part, HEX);
}

void loop()
{
    // en: A received packet would carry its CRC; here one is made, sent, damaged and checked.
    // ja: 受け取ったパケットにはCRCが付いてくる。ここでは作って、壊して、検査する。
    uint8_t packet[16];
    for (unsigned i = 0; i < sizeof packet; i++) {
        packet[i] = (uint8_t)(millis() + i);
    }
    const uint32_t sent = CH32RV.crc32(packet, sizeof packet);
    packet[3] ^= 0x10;   // en: one bit flipped on the way / ja: 途中で1ビット化けた
    const bool ok = CH32RV.crc32(packet, sizeof packet) == sent;
    Serial.println(ok ? "packet ok" : "packet damaged: CRC mismatch");
    delay(2000);
}
