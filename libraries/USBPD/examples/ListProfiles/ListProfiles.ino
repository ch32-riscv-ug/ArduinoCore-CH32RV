/* List what a USB PD charger offers, then ask it for 9 V.
 *
 * Runs on CH32X035/X033 (checked on a WeAct CH32X035F8U6 board); on the
 * other parts begin() is false and the sketch says so - see
 * libraries/USBPD/README.md.
 *
 * A board powered from the charger's own VBUS: a 9 V contract raises VBUS
 * to 9 V, so make sure the board's regulator takes it.
 */
#include <USBPD.h>

void setup() {
  Serial.begin(115200);

  if (!USBPD.begin()) {
    Serial.println("USB PD is not available on this board");
    return;
  }

  /* ready() moves the driver along by itself. Usually under a second; a
   * charger that only speaks Type-C (no PD) never gets there. */
  const uint32_t start = millis();
  while (!USBPD.ready()) {
    if (millis() - start > 5000) {
      Serial.println(USBPD.connected() ? "a charger without USB PD"
                                       : "no charger on the USB-C");
      return;
    }
  }

  Serial.println("the charger offers:");
  for (uint8_t i = 0; i < USBPD.profileCount(); i++) {
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

void loop() {
  USBPD.maintain();   /* keeps a PPS contract alive; harmless otherwise */
}
