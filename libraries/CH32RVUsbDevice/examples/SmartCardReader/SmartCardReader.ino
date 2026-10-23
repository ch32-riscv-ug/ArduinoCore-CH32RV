// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceCcid Ccid(device);

static const uint8_t CARD_ATR[] = {
    0x3b, 0x8f, 0x80, 0x01, 0x80, 0x4f, 0x0c, 0xa0, 0x00, 0x00,
    0x03, 0x06, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x6a,
};

static const uint8_t CARD_UID[] = {0x04, 0x11, 0x22, 0x33};

static uint32_t lastStatusMs = 0;

// en: APDU provider runs inside USB processing; responses include SW1/SW2.
// ja: USB処理中のAPDU provider。応答にはSW1/SW2も含める。
static size_t answerApdu(const uint8_t *apdu, size_t length, uint8_t *response, size_t capacity)
{
  Serial.printf("APDU len=%u:", static_cast<unsigned>(length));
  for (size_t i = 0; i < length; i++)
  {
    Serial.printf(" %02x", apdu[i]);
  }
  Serial.println();

  if (length >= 4 && apdu[0] == 0xff && apdu[1] == 0xca)
  {
    if (capacity < sizeof(CARD_UID) + 2)
    {
      return 0;
    }
    memcpy(response, CARD_UID, sizeof(CARD_UID));
    response[sizeof(CARD_UID)] = 0x90;
    response[sizeof(CARD_UID) + 1] = 0x00;
    return sizeof(CARD_UID) + 2;
  }

  if (length >= 5 && apdu[0] == 0x80 && apdu[1] == 0x01)
  {
    const size_t dataLength = apdu[4];
    // en: Check both the received body length and reply capacity before copying.
    // ja: コピー前に、受信body長と応答先の容量の両方を確認する。
    if (length < 5 + dataLength || capacity < dataLength + 2)
    {
      return 0;
    }
    memcpy(response, &apdu[5], dataLength);
    response[dataLength] = 0x90;
    response[dataLength + 1] = 0x00;
    return dataLength + 2;
  }

  // en: Unsupported instruction status (6D00). / ja: 未対応の命令を示すstatus（6D00）。
  response[0] = 0x6d;
  response[1] = 0x00;
  return 2;
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(1500);

  Ccid.onApdu(answerApdu);
  Ccid.onPower([](bool on)
               { Serial.printf("CARD %s\n", on ? "activated" : "deactivated"); });

  CH32RVUsbDeviceConfig config;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice CCID Reader";
  config.serialNumber = "ch32rv-ccid";
  config.pid = 0x0001;
  if (!device.begin(config))
  {
    Serial.printf("begin failed: %s\n", device.lastErrorName());
    return;
  }

  Ccid.insertCard(CARD_ATR, sizeof(CARD_ATR));
  Serial.println("CCID reader ready. 'i' inserts a card, 'r' removes it.");
}

void loop()
{
  // en: Deliver notifications here. / ja: 通知callbackはここで実行する。
  deliverEvents();
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == 'i')
    {
      Serial.printf("insert %u\n", Ccid.insertCard(CARD_ATR, sizeof(CARD_ATR)) ? 1 : 0);
    }
    else if (command == 'r')
    {
      Ccid.removeCard();
      Serial.println("removed");
    }
  }

  if (millis() - lastStatusMs >= 2000)
  {
    lastStatusMs = millis();
    Serial.printf("mounted=%u card=%u powered=%u commands=%lu apdus=%lu\n",
                  Ccid.mounted() ? 1 : 0,
                  Ccid.cardPresent() ? 1 : 0,
                  Ccid.cardPowered() ? 1 : 0,
                  static_cast<unsigned long>(Ccid.commandCount()),
                  static_cast<unsigned long>(Ccid.apduCount()));
  }
  delay(10);
}
