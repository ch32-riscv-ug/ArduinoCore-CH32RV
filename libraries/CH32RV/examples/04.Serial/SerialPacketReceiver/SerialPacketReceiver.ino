// en: Receive length-prefixed UART packets with a timeout and checksum.
// ja: 長さ付きUARTパケットをtimeoutとchecksumを確認しながら受信する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

static const uint8_t START_BYTE = 0xA5;
static const uint8_t MAX_PAYLOAD = 16;
static const uint32_t PACKET_TIMEOUT_MS = 1000;
enum ReceiveState { WAIT_START, WAIT_LENGTH, WAIT_PAYLOAD, WAIT_CHECKSUM };
static ReceiveState state = WAIT_START;
static uint8_t payload[MAX_PAYLOAD];
static uint8_t length, received, checksum;
static uint32_t lastByteMs;

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    Serial.println("Packet: A5 length payload... XOR(length,payload...)");
}

void loop()
{
    if (state != WAIT_START && (uint32_t)(millis() - lastByteMs) >= PACKET_TIMEOUT_MS) {
        Serial.println("Packet timed out");
        state = WAIT_START;
    }
    for (uint8_t budget = 0; budget < 16 && Serial.available(); ++budget) {
        const uint8_t byte = (uint8_t)Serial.read();
        lastByteMs = millis();
        switch (state) {
        case WAIT_START:
            if (byte == START_BYTE) {
                state = WAIT_LENGTH;
            }
            break;
        case WAIT_LENGTH:
            // en: Reject an unsafe length before writing any payload bytes.
            // ja: payloadを書き始める前に、不正な長さを拒否する。
            if (byte == 0 || byte > sizeof(payload)) {
                Serial.println("Invalid length");
                state = WAIT_START;
                break;
            }
            length = byte;
            received = 0;
            checksum = byte;
            state = WAIT_PAYLOAD;
            break;
        case WAIT_PAYLOAD:
            // en: A5 inside the payload is data, not another packet's start.
            // ja: payload内のA5はデータとして扱い、新しい開始byteとは解釈しない。
            payload[received++] = byte;
            checksum ^= byte;
            if (received == length) {
                state = WAIT_CHECKSUM;
            }
            break;
        case WAIT_CHECKSUM:
            if (byte == checksum) {
                Serial.print("Packet bytes:");
                for (uint8_t i = 0; i < length; ++i) {
                    Serial.print(' ');
                    Serial.print(payload[i], HEX);
                }
                Serial.println();
            } else {
                Serial.println("Checksum mismatch");
            }
            // en: After success or failure, search for the next start byte.
            // ja: 成功時も失敗時も、次の開始byteを探す状態へ戻す。
            state = WAIT_START;
            break;
        }
    }
}
