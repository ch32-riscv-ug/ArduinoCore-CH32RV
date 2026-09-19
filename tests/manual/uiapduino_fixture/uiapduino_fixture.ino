#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

static char command[64];
static size_t used;

static void adcCommand(unsigned pin, unsigned waitMs) {
  delay(waitMs);
  uint32_t sum = 0;
  int minimum = 1024;
  int maximum = 0;
  for (int sample = 0; sample < 32; ++sample) {
    const int value = analogRead(pin);
    sum += value;
    if (value < minimum) minimum = value;
    if (value > maximum) maximum = value;
  }
  Serial.print("ADC pin="); Serial.print(pin);
  Serial.print(" avg="); Serial.print(sum / 32);
  Serial.print(" min="); Serial.print(minimum);
  Serial.print(" max="); Serial.println(maximum);
}

static void adcWaitCommand(unsigned pin, unsigned beforeMs, unsigned afterMs) {
  // Select the ADC mux before the fixture changes the electrical level. This
  // is especially important for PD5: it was the UART TX output that carried
  // this command, and must become high impedance before the jig drives it.
  Serial.end();
  (void)analogRead(pin);
  delay(beforeMs);
  const int value = analogRead(pin);
  delay(afterMs);
  Serial.begin(115200);
  delay(20);  // establish UART idle HIGH before the first start bit
  // The fixture drove this same wire while it was an ADC input.  Repeat the
  // idempotent report so the first frame can serve as UART resynchronization.
  for (unsigned report = 0; report < 3; ++report) {
    Serial.print("ADCWAIT pin="); Serial.print(pin);
    Serial.print(" value="); Serial.println(value);
    delay(50);
  }
}

static void adcSweepCommand(unsigned pin, unsigned stepMs) {
  Serial.end();
  (void)analogRead(pin);
  int values[3];
  for (unsigned phase = 0; phase < 3; ++phase) {
    delay(stepMs);
    values[phase] = analogRead(pin);
  }
  Serial.begin(115200);
  delay(20);
  for (unsigned report = 0; report < 3; ++report) {
    Serial.print("ADCSWEEP pin="); Serial.print(pin);
    Serial.print(" values="); Serial.print(values[0]);
    Serial.print(','); Serial.print(values[1]);
    Serial.print(','); Serial.println(values[2]);
    delay(50);
  }
}

static void execute(char *line) {
  unsigned pin = 0, value = 0, after = 0;
  // UART2 can present one empty line while the ESP32 fixture changes the pin
  // mux.  It is transport settling, not a DUT command failure.
  if (!*line) {
    return;
  } else if (!strcmp(line, "PING")) {
    Serial.println("PONG");
  } else if (!strcmp(line, "I2C")) {
    Wire.begin();
    Wire.beginTransmission(0x42);
    Wire.write((uint8_t)0x12);
    Wire.write((uint8_t)0x34);
    const uint8_t status = Wire.endTransmission();
    const size_t count = Wire.requestFrom((uint8_t)0x42, (size_t)4);
    Serial.print("I2C status="); Serial.print(status);
    Serial.print(" count="); Serial.print(count);
    Serial.print(" data=");
    while (Wire.available()) {
      const int byte = Wire.read();
      if (byte < 16) Serial.print('0');
      Serial.print(byte, HEX);
    }
    Serial.println();
  } else if (!strcmp(line, "I2CPREP")) {
    // Release SDA/SCL from earlier GPIO output tests before the ESP32 starts
    // its slave peripheral.  A held-low bus makes ESP-IDF reject slave init.
    pinMode(SDA, INPUT_PULLUP);
    pinMode(SCL, INPUT_PULLUP);
    Serial.println("I2CPREP ready");
  } else if (!strcmp(line, "SPI")) {
    const uint8_t sent[] = {0x31, 0x42, 0x53, 0x64};
    uint8_t received[sizeof(sent)] = {};
    pinMode(SS, OUTPUT);
    digitalWrite(SS, HIGH);
    SPI.begin();
    SPI.beginTransaction(SPISettings(500000, MSBFIRST, SPI_MODE0));
    digitalWrite(SS, LOW);
    for (size_t i = 0; i < sizeof(sent); ++i) received[i] = SPI.transfer(sent[i]);
    digitalWrite(SS, HIGH);
    SPI.endTransaction();
    Serial.print("SPI data=");
    for (uint8_t byte : received) {
      if (byte < 16) Serial.print('0');
      Serial.print(byte, HEX);
    }
    Serial.println();
  } else if (sscanf(line, "DIN %u %u", &pin, &value) == 2) {
    pinMode(pin, INPUT);
    delay(value);
    Serial.print("DIN pin="); Serial.print(pin);
    Serial.print(" value="); Serial.println(digitalRead(pin));
  } else if (sscanf(line, "ADC %u %u", &pin, &value) == 2) {
    adcCommand(pin, value);
  } else if (sscanf(line, "ADCWAIT %u %u %u", &pin, &value, &after) == 3) {
    adcWaitCommand(pin, value, after);
  } else if (sscanf(line, "ADCSWEEP %u %u", &pin, &value) == 2) {
    adcSweepCommand(pin, value);
  } else if (sscanf(line, "PULSE %u %u", &pin, &value) == 2) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
    delay(value);
    digitalWrite(pin, LOW);
    pinMode(pin, INPUT);
    Serial.begin(115200);
    Serial.print("PULSE pin="); Serial.print(pin);
    Serial.println(" done");
  } else if (sscanf(line, "DOUT %u %u", &pin, &value) == 2) {
    digitalWrite(pin, value ? HIGH : LOW);
    pinMode(pin, OUTPUT);
    Serial.print("DOUT pin="); Serial.print(pin);
    Serial.print(" value="); Serial.println(value ? 1 : 0);
  } else if (sscanf(line, "PULLUP %u", &pin) == 1) {
    pinMode(pin, INPUT_PULLUP);
    delay(1);
    Serial.print("PULLUP pin="); Serial.print(pin);
    Serial.print(" value="); Serial.println(digitalRead(pin));
  } else if (sscanf(line, "PULLDOWN %u", &pin) == 1) {
    pinMode(pin, INPUT_PULLDOWN);
    delay(1);
    Serial.print("PULLDOWN pin="); Serial.print(pin);
    Serial.print(" value="); Serial.println(digitalRead(pin));
  } else {
    Serial.print("ERROR command="); Serial.println(line);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("UIAP FIXTURE READY core=ArduinoCore-CH32");
}

void loop() {
  while (Serial.available()) {
    const int value = Serial.read();
    if (value == '\r') continue;
    if (value == '\n') {
      command[used] = 0;
      execute(command);
      used = 0;
    } else if (used + 1 < sizeof(command)) {
      command[used++] = (char)value;
    } else {
      used = 0;
      Serial.println("ERROR command_too_long");
    }
  }
}
