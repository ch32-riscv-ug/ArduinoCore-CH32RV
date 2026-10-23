// en: Read a compatible SPI Flash JEDEC ID without changing stored data.
// ja: 対応するSPI FlashのJEDEC IDを、保存内容を書き換えずに読む。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: SPI1 */

#include <SPI.h>

// en: CS is a separate GPIO; some variants have no default SS alias.
// ja: CSは別のGPIOで指定する。SSの既定別名がないvariantもある。
#ifdef SS
static const uint8_t CS_PIN = SS;
#else
static const uint8_t CS_PIN = PA4;
#endif
static bool busReady;

static void readId()
{
    uint8_t id[3];
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    // en: Keep CS low across the command and all response bytes; Arduino manages it as GPIO.
    // ja: commandから応答の終わりまでCSをLowに保つ。CSはGPIOとして操作する。
    digitalWrite(CS_PIN, LOW);
    SPI.transfer(0x9F);
    for (uint8_t i = 0; i < sizeof(id); ++i) {
        // en: Sending a dummy byte generates the clock needed to receive each response byte.
        // ja: dummy byteを送ってclockを作り、応答を1 byteずつ受け取る。
        id[i] = SPI.transfer(0x00);
    }
    digitalWrite(CS_PIN, HIGH);
    SPI.endTransaction();
    Serial.print("JEDEC ID:");
    for (uint8_t i = 0; i < sizeof(id); ++i) {
        Serial.print(' ');
        Serial.print(id[i], HEX);
    }
    Serial.println();
    if ((id[0] == 0 && id[1] == 0 && id[2] == 0) ||
        (id[0] == 0xFF && id[1] == 0xFF && id[2] == 0xFF)) {
        // en: SPI has no address ACK; suspicious data is a wiring clue, not a transport error code.
        // ja: SPIにはアドレスACKがない。不自然な値は配線確認の手掛かりで、通信のエラーコードではない。
        Serial.println("Check power, CS, MISO and command compatibility");
    }
}

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    // en: Deselect the chip before starting the bus so initialization cannot become a command.
    // ja: バスの開始前にCSを非選択にし、初期化時の変化をcommandとして受け取らせない。
    digitalWrite(CS_PIN, HIGH);
    pinMode(CS_PIN, OUTPUT);
    busReady = SPI.begin(SCK, MISO, MOSI);
    if (!busReady) {
        Serial.println("SPI begin failed; check pin routes");
        return;
    }
    readId();
    Serial.println("Send i to read again");
}

void loop()
{
    if (busReady && Serial.available() && Serial.read() == 'i') {
        readId();
    }
}
