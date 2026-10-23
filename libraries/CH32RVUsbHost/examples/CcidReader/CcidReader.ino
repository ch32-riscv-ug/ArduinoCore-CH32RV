// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。
/* requires: USBFS */

#include <CH32RVUsbHost.h>

CH32RVUsbHost usb;
static bool check_card;

void setup() {
  Serial.begin(115200);
  usb.onDeviceConnected([](const CH32RVUsbHostDeviceInfo &) { check_card = true; });
  usb.onCcidCardInserted([](const CH32RVUsbHostCcidSlotEvent &) { check_card = true; });
  usb.onCcidCardRemoved([](const CH32RVUsbHostCcidSlotEvent &) { Serial.println("card removed"); });
  if (!usb.begin()) {
    Serial.print("usb.begin() failed: ");
    Serial.println(usb.lastErrorName());
  }
}

void loop() {
  // en: Deliver notifications here. / ja: 通知callbackはここで実行する。
  deliverEvents();
  if (!check_card) {
    return;
  }
  check_card = false;
  if (!usb.ccidReady() && !usb.ccidOpen()) {
    return;
  }
  uint8_t atr[33];
  size_t atrLength = 0;
  // en: Reader readiness does not guarantee a present, activated card. / ja: リーダーの準備完了だけでは、カードの存在と有効化は保証しない。
  if (!usb.ccidPowerOn(atr, sizeof atr, &atrLength)) {
    Serial.println("no card");
    return;
  }
  Serial.print("ATR: ");
  ch32rvUsbHostPrintHex(atr, atrLength);
  Serial.println();
  static const uint8_t select_mf[] = {0x00, 0xA4, 0x00, 0x00, 0x02, 0x3F, 0x00};
  uint8_t response[64];
  size_t length = 0;
  uint16_t sw = 0;
  // en: Transport success does not imply APDU success; inspect sw separately. / ja: 交換の成功とAPDUの成功は別。swも確認する。
  if (usb.ccidApdu(select_mf, sizeof select_mf, response, sizeof response, &length, &sw)) {
    Serial.print("SELECT MF: SW=");
    Serial.println(sw, HEX);
  }
}
