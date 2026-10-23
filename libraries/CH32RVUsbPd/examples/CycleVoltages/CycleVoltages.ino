// en: Use a button to cycle through a PD source's fixed-voltage profiles.
// ja: ボタンでPD電源の固定電圧プロファイルを順番に切り替える。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVUsbPd.h>

static const int RX_PIN = RX;
static const int TX_PIN = TX;
static const int BUTTON_PIN = PA1;

static void print_contract() {
  Serial.print("contract: ");
  Serial.print(USBPD.voltage());
  Serial.print(" mV, up to ");
  Serial.print(USBPD.current());
  Serial.println(" mA");
}

void setup() {
  Serial.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  if (!USBPD.begin()) {
    Serial.println("USB PD is not available on this board");
    return;
  }
  const uint32_t start = millis();
  // en: ready() advances negotiation; keep a deadline for a non-PD source.
  // ja: ready()で交渉も進む。PD非対応の相手に備えて期限を設ける。
  while (!USBPD.ready()) {
    if (millis() - start > 5000) {
      Serial.println(USBPD.connected() ? "a charger without USB PD" : "no charger on the USB-C");
      return;
    }
  }
  print_contract();
}

void loop() {
  static bool was_up = true;
  // en: Compare this level with the previous one so a held button does not request a new voltage every loop.
  // ja: 前回の状態と比べ、押している間の全巡回で次の電圧を要求しないようにする。
  const bool up = digitalRead(BUTTON_PIN) == HIGH;
  if (was_up && !up && USBPD.ready()) {
    // en: Search the current advertisement for the next fixed PDO, wrapping at the end.
    // ja: 現在の広告から次の固定PDOを探し、末尾なら先頭へ戻る。
    const uint8_t count = USBPD.profileCount();
    uint8_t next = USBPD.contractProfile();
    for (uint8_t tries = 0; tries < count; tries++) {
      next = (uint8_t)((next + 1) % count);
      if (USBPD.profile(next).kind == PD_SUPPLY_FIXED) {
        break;
      }
    }
    if (USBPD.requestProfile(next)) {
      print_contract();
    } else {
      Serial.println("the charger refused");
    }
  }
  was_up = up;
  delay(10);
}
