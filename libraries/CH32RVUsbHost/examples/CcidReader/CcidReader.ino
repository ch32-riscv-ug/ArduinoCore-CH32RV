// en: Activate a card in a USB reader and exchange a SELECT MF command.
// ja: USBリーダーのカードを有効化し、SELECT MFコマンドを交換する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: USBFS */

#include <CH32RVUsbHost.h>

CH32RVUsbHost usb;
static bool check_card;

void setup() {
  Serial.begin(115200);
  usb.onDeviceConnected([](const CH32RVUsbHostDeviceInfo &) { check_card = true; });
  usb.onCcidCardInserted([](const CH32RVUsbHostCcidSlotEvent &) { check_card = true; });
  usb.onCcidCardRemoved([](const CH32RVUsbHostCcidSlotEvent &) { Serial.println("card removed"); });
  // en: Start the host controller; connected peripherals are discovered later through USB processing.
  // ja: Host controllerを開始する。接続機器は、その後のUSB処理で検出・列挙される。
  if (!usb.begin()) {
    Serial.print("usb.begin() failed: ");
    Serial.println(usb.lastErrorName());
  }
}

void loop() {
  // en: Run registered notification handlers here; USB transfer processing runs separately.
  // ja: 登録した通知関数をここで実行する。USB転送そのものの処理とは別。
  deliverEvents();
  if (!check_card) {
    return;
  }
  // en: Consume this check request once; another connection or insertion event can schedule the next check.
  // ja: 今回の確認要求を一度消す。次の接続や挿入通知で、また確認を予約する。
  check_card = false;
  if (!usb.ccidReady() && !usb.ccidOpen()) {
    return;
  }
  uint8_t atr[33];
  size_t atrLength = 0;
  // en: Reader readiness does not guarantee a present, activated card.
  // ja: リーダーの準備完了だけでは、カードの存在と有効化は保証しない。
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
  // en: Transport success does not imply APDU success; inspect sw separately.
  // ja: 交換の成功とAPDUの成功は別。swも確認する。
  if (usb.ccidApdu(select_mf, sizeof select_mf, response, sizeof response, &length, &sw)) {
    Serial.print("SELECT MF: SW=");
    Serial.println(sw, HEX);
  }
}
