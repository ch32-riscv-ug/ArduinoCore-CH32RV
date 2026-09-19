#include <Arduino.h>
#include <CH32Timer.h>

static char command[24];
static size_t used;
static volatile unsigned timerTicks;
static volatile unsigned timerQuiesced;
static const uint8_t ownerAIdentity = 0;
static const uint8_t ownerBIdentity = 0;

static void timerUpdate(void *) { ++timerTicks; }
static void timerQuiesce(void *, uint8_t, uint8_t) { ++timerQuiesced; }

static void testTimerResource() {
  const CH32TimerRequest request = {2, CH32_TIMER_WHOLE, {0, 23999, 0}};
  const CH32TimerOwner ownerA = {&ownerAIdentity, timerQuiesce, nullptr};
  const CH32TimerOwner ownerB = {&ownerBIdentity, nullptr, nullptr};
  timerTicks = 0;
  timerQuiesced = 0;
  CH32TimerLease displaced = ch32TimerTryAcquire(&request, &ownerA);
  CH32TimerLease active = ch32TimerTakeover(&request, &ownerB);
  const bool invalidated = !ch32TimerLeaseValid(&displaced);
  const bool started = ch32TimerApplyBase(&active) &&
                       ch32TimerAttachUpdateInterrupt(&active, timerUpdate,
                                                      nullptr) &&
                       ch32TimerStart(&active);
  delay(30);
  const unsigned ticks = timerTicks;
  ch32TimerDetachUpdateInterrupt(&active);
  ch32TimerRelease(&active);
  CH32TimerStatus status = {};
  const bool statusRead = ch32TimerGetStatus(2, &status);
  Serial.print("TIMER started="); Serial.print(started ? 1 : 0);
  Serial.print(" invalidated="); Serial.print(invalidated ? 1 : 0);
  Serial.print(" quiesced="); Serial.print(timerQuiesced);
  Serial.print(" ticks="); Serial.print(ticks);
  Serial.print(" released=");
  Serial.println(statusRead && !status.configured ? 1 : 0);
}

static void execute(const char *line) {
  if (!strcmp(line, "PING")) {
    Serial.println("PONG");
  } else if (!strcmp(line, "MILLIS")) {
    const unsigned long before = millis();
    delay(125);
    Serial.print("MILLIS delta="); Serial.println(millis() - before);
  } else if (!strcmp(line, "TIMER")) {
    testTimerResource();
  } else if (!strcmp(line, "PWM")) {
    analogWrite(PC3, 64);
    Serial.println("PWM pin=PC3 value=64");
  } else if (!strcmp(line, "PWMSTOP")) {
    pinMode(PC3, OUTPUT);
    digitalWrite(PC3, LOW);
    Serial.println("PWM stopped");
  } else if (!strcmp(line, "TONE")) {
    tone(PC0, 1000, 400);
    Serial.println("TONE pin=PC0 frequency=1000 duration=400");
  } else if (!strcmp(line, "TONESTOP")) {
    noTone(PC0);
    Serial.println("TONE stopped");
  } else {
    Serial.print("ERROR command="); Serial.println(line);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("UIAP TIMER FIXTURE READY");
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
    }
  }
}
