// en: List a PD source's capabilities and request a 9 V contract.
// ja: PD電源の能力を一覧表示し、9 Vの契約を要求する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVUsbPd.h>

void setup() {
  Serial.begin(115200, SERIAL_8N1, RX, TX);

  if (!USBPD.begin()) {
    Serial.println("USB PD is not available on this board");
    return;
  }

  const uint32_t start = millis();
  // en: ready() advances negotiation; connected() alone is not an explicit contract.
  // ja: ready()で交渉も進む。connected()だけでは明示契約の成立ではない。
  while (!USBPD.ready()) {
    if (millis() - start > 5000) {
      Serial.println(USBPD.connected() ? "a charger without USB PD"
                                       : "no charger on the USB-C");
      return;
    }
  }

  Serial.println("the charger offers:");
  for (uint8_t i = 0; i < USBPD.profileCount(); i++) {
    // en: Each entry describes an advertised supply option; values are millivolts and milliamps.
    // ja: 各項目は相手が広告した供給候補。値の単位はmVとmA。
    PDProfile p = USBPD.profile(i);
    Serial.print("  [");
    Serial.print(i);
    Serial.print("] ");
    Serial.print(pd_supply_name(p.kind));
    Serial.print(" ");
    Serial.print(p.min_mv);
    if (p.max_mv != p.min_mv) {
      Serial.print("-");
      Serial.print(p.max_mv);
    }
    Serial.print(" mV, ");
    Serial.print(p.max_ma);
    Serial.println(" mA");
  }

  // en: Voltage is in millivolts; a successful request can change physical VBUS.
  // ja: 電圧はmV単位。要求が成立すると実際のVBUS電圧も変わる。
  if (USBPD.request(9000)) {
    Serial.print("now at ");
    Serial.print(USBPD.voltage());
    Serial.print(" mV, up to ");
    Serial.print(USBPD.current());
    Serial.print(" mA (profile ");
    Serial.print(USBPD.contractProfile());
    Serial.println(")");
  } else {
    Serial.println("this charger has no 9 V");
  }
}

// en: Returning from loop() lets the registered service maintain the contract.
// ja: loop()から戻ると、登録済みのserviceが契約を維持する。
void loop() {
}
