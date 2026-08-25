/* List what a USB PD charger offers, then ask it for 9 V.
 *
 * This example is the intended shape of the API. The hardware driver
 * underneath is still being written, so today begin() reports that USB PD is
 * not available and the sketch says so - see libraries/USBPD/README.md.
 *
 * Works only on parts with a USBPD block (CH32X035/X033, CH32L103/M103,
 * CH32V205, CH32X315, CH32H417, CH32M030); everywhere else begin() is false.
 */
#include <USBPD.h>

void setup() {
  Serial.begin(115200);

  if (!USBPD.begin()) {
    Serial.println("USB PD is not available on this board");
    return;
  }

  while (!USBPD.ready()) {
    /* waiting for the charger to enumerate */
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
    Serial.println(" mV");
  } else {
    Serial.println("this charger has no 9 V");
  }
}

void loop() {
  USBPD.maintain();   /* keeps a PPS contract alive; harmless otherwise */
}
