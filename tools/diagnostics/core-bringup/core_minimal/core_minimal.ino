#include <Arduino.h>
#include <SerialDMSeq.h>

volatile uint32_t dataMarker = 0x13579bdf;
volatile uint32_t bssMarker;
volatile uint32_t ctorMarker;
struct Constructor {
  Constructor() { ctorMarker = 0x2468ace0; }
} constructor;

void setup() {
  SerialDMSeq.begin(115200);
}

void loop() {
  if (SerialDMSeq.available() && SerialDMSeq.read() == 'R') {
    bool initOk = dataMarker == 0x13579bdf && bssMarker == 0 &&
                  ctorMarker == 0x2468ace0;
    unsigned long ms = millis();
    delay(100);
    unsigned long elapsedMs = millis() - ms;
    unsigned long us = micros();
    delayMicroseconds(2000);
    unsigned long elapsedUs = micros() - us;
    bool timeOk = elapsedMs >= 90 && elapsedMs <= 150 &&
                  elapsedUs >= 1500 && elapsedUs <= 4000;
    SerialDMSeq.print("BRINGUP init=");
    SerialDMSeq.print(initOk ? "PASS" : "FAIL");
    SerialDMSeq.print(" millis=");
    SerialDMSeq.print(elapsedMs);
    SerialDMSeq.print(" micros=");
    SerialDMSeq.print(elapsedUs);
    SerialDMSeq.print(" F_CPU=");
    SerialDMSeq.println(F_CPU);
    SerialDMSeq.println(initOk && timeOk ? "BRINGUP PASS" : "BRINGUP FAIL");
  }
}
