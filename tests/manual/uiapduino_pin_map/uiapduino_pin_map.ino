#include <Arduino.h>

// UIAPduino Pro Micro CH32V003 V1.4 external-header GPIOs, in Arduino pin
// order. PD1/SWIO, PD7/RESET, and the PD3/PD4 USB data pair are deliberately
// not configured, read, or driven by this fixture.
static const pin_size_t kHeaderPins[] = {
  PA1, PA2, PC0, PC1, PC2, PC3, PC4, PC5,
  PC6, PC7, PD0, PD2, PD5, PD6,
};

static void allLow() {
  for (pin_size_t pin : kHeaderPins) digitalWrite(pin, LOW);
}

void setup() {
  for (pin_size_t pin : kHeaderPins) {
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
  }
}

void loop() {
  // A distinctive all-HIGH marker delimits each mapping cycle.
  for (pin_size_t pin : kHeaderPins) digitalWrite(pin, HIGH);
  delay(1000);
  allLow();
  delay(500);

  for (pin_size_t pin : kHeaderPins) {
    digitalWrite(pin, HIGH);
    delay(500);
    digitalWrite(pin, LOW);
    delay(250);
  }
}
