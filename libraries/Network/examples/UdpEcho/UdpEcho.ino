// en: A UDP echo on port 9000: each datagram goes back to its sender.
// ja: ポート9000のUDPエコー。届いたデータグラムを、送り主へそのまま返す。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: ETH_MAC */

#include <Network.h>

static const uint16_t PORT = 9000;

static NetworkUDP udp;
static uint8_t buf[512];
static uint32_t packets;

void setup()
{
    Serial.begin(115200);
    delay(100);
    if (!ETH.begin()) {
        Serial.println("ETH.begin() failed");
        return;
    }
    while (!ETH.hasIP()) {
        delay(100);
    }
    udp.begin(PORT);
    Serial.print("UDP echo on ");
    Serial.print(ETH.localIP());
    Serial.print(':');
    Serial.println(PORT);
}

void loop()
{
    // en: One datagram per parsePacket(); a datagram longer than buf is cut, the rest is dropped.
    // ja: parsePacket()1回で1データグラム。bufより長い分は切り捨てられる。
    const int size = udp.parsePacket();
    if (size <= 0) {
        return;
    }
    const int n = udp.read(buf, sizeof buf);
    if (n < 0) {
        return;
    }
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.write(buf, (size_t)n);
    const bool sent = udp.endPacket();
    packets++;
    Serial.print('#');
    Serial.print(packets);
    Serial.print(' ');
    Serial.print(udp.remoteIP());
    Serial.print(':');
    Serial.print(udp.remotePort());
    Serial.print(' ');
    Serial.print(size);
    Serial.print(" bytes");
    if (size > (int)sizeof buf) {
        Serial.print(" (cut to ");
        Serial.print(n);
        Serial.print(')');
    }
    Serial.println(sent ? "" : " - reply not sent");
}
