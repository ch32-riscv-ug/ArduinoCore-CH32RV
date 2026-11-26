// en: A USB-serial style bridge: what Serial receives goes out on Serial1, and back.
// ja: シリアルの中継: Serialが受けたものをSerial1へ、Serial1が受けたものをSerialへ送る。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: SERIAL1 */

static const uint32_t BAUD = 115200;

void setup()
{
    Serial.begin(BAUD);
    Serial1.begin(BAUD);
}

void loop()
{
    // en: Move what is there each way, as much as the other side can take now (no byte waits for the other).
    // ja: それぞれの向きで、届いた分を、相手が今受け取れるだけ送る（片方の向きが他方を待たせない）。
    uint8_t buf[32];
    size_t n = Serial.available();
    if (n) {
        const size_t room = Serial1.availableForWrite();
        n = n < sizeof buf ? n : sizeof buf;
        n = room && n > room ? room : n;
        n = Serial.readBytes(buf, n);
        Serial1.write(buf, n);
    }
    n = Serial1.available();
    if (n) {
        const size_t room = Serial.availableForWrite();
        n = n < sizeof buf ? n : sizeof buf;
        n = room && n > room ? room : n;
        n = Serial1.readBytes(buf, n);
        Serial.write(buf, n);
    }
}
