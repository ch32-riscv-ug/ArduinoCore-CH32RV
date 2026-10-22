/* Wiring, operation and expected output: README.md / README.ja.md. */
/* Be a USB PD source: offer 5 V and 9 V fixed and a 3.3-11 V PPS range to
 * whatever sink is plugged into the USB-C, and print each contract.
 *
 * Controller support and wiring: see the adjacent README files. The board must not have Rd resistors on CC (the
 * WeAct CH32X035 board has them as R4/R5 - remove both), and the actual
 * supply is yours: the two callbacks below switch it. They are required;
 * begin() fails without them. Each returns true only once the supply has
 * done what it was asked - this sketch has no supply, so it says so and
 * returns true (replace the bodies with your load switch and regulator).
 */
#include <CH32RVUsbPd.h>

bool vbus(bool on) {
  Serial.println(on ? "VBUS on (5 V)" : "VBUS off");
  return true;
}

bool setVoltage(uint16_t mv, uint16_t ma, bool pps) {
  Serial.print(pps ? "PPS " : "fixed ");
  Serial.print(mv);
  Serial.print(" mV, up to ");
  Serial.print(ma);
  Serial.println(" mA");
  return true;
}

void setup() {
  Serial.begin(115200, SERIAL_8N1, RX, TX);   /* RX, TX: the pads you wired */

  USBPDSource.setRpCurrent(PD_RP_3A0);
  USBPDSource.addFixed(5000, 3000);           /* the first one must be 5 V fixed */
  USBPDSource.addFixed(9000, 2000);
  USBPDSource.addPps(3300, 11000, 2000);
  USBPDSource.onVbus(vbus);
  USBPDSource.onSetVoltage(setVoltage);
  if (!USBPDSource.begin()) {
    Serial.print("USB PD source not started: ");
    Serial.println(USBPDSource.lastErrorName());
    return;
  }
  Serial.println("waiting for a sink");
}

void loop() {
  static int8_t shown = -2;
  const int8_t now = USBPDSource.ready() ? USBPDSource.contractProfile() : -1;
  if (now != shown) {
    shown = now;
    if (now >= 0) {
      Serial.print("contract: profile ");
      Serial.println(now + 1);
    } else if (USBPDSource.attached()) {
      Serial.println("sink attached, no contract");
    }
  }
}
