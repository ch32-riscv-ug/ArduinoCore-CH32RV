/* CycleVoltages - step through a USB PD charger's fixed voltages with a button.
 *
 * Each press asks for the next fixed profile the charger offers (5 V, 9 V,
 * 12 V ... and back to 5 V) and prints the contract on the UART. The driver
 * keeps the link itself as long as loop() calls maintain().
 *
 * Wiring: the board's USB-C to a PD charger, a push button from BUTTON_PIN to
 * GND, and the UART (RX_PIN / TX_PIN, the chip's default pads - change them to
 * yours) to a USB-serial adapter. A board powered from the charger's VBUS sees
 * every voltage it asks for: make sure its regulator takes the highest one.
 *
 * Runs on CH32X035/X033; on other series begin() is false.
 */
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
  while (!USBPD.ready()) {
    if (millis() - start > 5000) {
      Serial.println(USBPD.connected() ? "a charger without USB PD" : "no charger on the USB-C");
      return;
    }
  }
  print_contract();
}

void loop() {
  USBPD.maintain();

  static bool was_up = true;
  const bool up = digitalRead(BUTTON_PIN) == HIGH;
  if (was_up && !up && USBPD.ready()) {
    /* The next fixed profile after the current one, wrapping to profile 0. */
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
