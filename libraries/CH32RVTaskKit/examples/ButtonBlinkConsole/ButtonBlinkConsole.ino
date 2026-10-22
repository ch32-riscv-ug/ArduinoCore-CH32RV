/* Wiring, operation and expected output: README.md / README.ja.md. */
/* ButtonBlinkConsole - a small program put together from pieces of work, the
 * shape docs/sketch-guide.ja.md builds up to. Four Works, each started by a
 * different trigger:
 *
 *   blink    a Period             toggles the LED
 *   button   post() from the ISR  handles the presses the ISR queued
 *   console  a condition          reads UART bytes into a line, runs commands
 *   resume   a Deadline           ends a pause
 *
 * Nothing here waits: each Work does what it can and returns, and loop() is
 * one runOnce(). The ISR only records; the reading of its two counters is the
 * one place interrupts are turned off.
 * docs: ../../README.ja.md describes the library. */
#include <CH32RVTaskKit.h>
#include <stdlib.h>
#include <string.h>

static const int LED_PIN = PC0;      /* an LED (with its resistor): change to your pin */
static const int BUTTON_PIN = PA1;   /* a push button to GND: change to your pin */

static CH32RVTaskKit::Executor app;

/* ---- what the ISR shares with loop() */
static CH32RVSyncKit::Queue<uint32_t, 8> presses;   /* press times, in order */
static volatile uint32_t isrCount;                  /* these two are written together, */
static volatile uint32_t isrLastMs;                 /* so they are read together */

/* ---- blink: on a Period */
static CH32RVSyncKit::Period blinkPeriod(500);
static bool ledOn = false;
static bool paused = false;

static void blinkStep()
{
  if (paused)
  {
    return;
  }
  ledOn = !ledOn;
  digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
}

/* ---- resume: on a Deadline */
static CH32RVSyncKit::Deadline pauseEnds;

static void resumeStep()
{
  if (!pauseEnds.expired())
  {
    return;             /* not armed: the first round after attach() */
  }
  pauseEnds.disarm();   /* once: until the next pause arms it again */
  paused = false;
  Serial.println("RESUME");
}

/* ---- button: posted by the ISR */
static void buttonStep()
{
  static uint32_t lastAccepted;
  uint32_t t;
  while (presses.tryReceive(t))
  {
    if (t - lastAccepted < 50)
    {
      continue;   /* a bounce of the same press */
    }
    lastAccepted = t;
    paused = !paused;
    Serial.print("PRESS ms=");
    Serial.print(t);
    Serial.println(paused ? " paused" : " running");
  }
}

static CH32RVTaskKit::Work button(buttonStep);

static void onPress()
{
  /* Record and return: no printing, no waiting in an ISR. */
  presses.trySend(millis());   /* a full queue drops this press; loop() catches up */
  isrCount = isrCount + 1;
  isrLastMs = millis();
  button.post();
}

/* ---- console: whenever the UART has bytes */
static CH32RVTaskKit::WorkStats blinkStats, buttonStats, consoleStats;
static CH32RVTaskKit::RoundStats rounds;

static void printStats(const char *name, const CH32RVTaskKit::WorkStats &s)
{
  Serial.print(name);
  Serial.print(" runs=");
  Serial.print(s.runs);
  Serial.print(" max_us=");
  Serial.println(s.maxUs);
}

static void runCommand(const char *line)
{
  if (strncmp(line, "rate ", 5) == 0)
  {
    const long ms = atol(line + 5);
    if (ms < 20 || ms > 5000)
    {
      Serial.println("ERR rate 20..5000");
      return;
    }
    blinkPeriod = CH32RVSyncKit::Period((uint32_t)ms);
    blinkPeriod.start();
    Serial.print("RATE ms=");
    Serial.println(ms);
  }
  else if (strcmp(line, "pause") == 0)
  {
    paused = true;
    pauseEnds.arm(5000);
    Serial.println("PAUSE 5 s");
  }
  else if (strcmp(line, "stats") == 0)
  {
    uint32_t count, lastMs;
    {
      CH32RVSyncKit::CriticalSection cs;   /* a consistent pair: copy, nothing else */
      count = isrCount;
      lastMs = isrLastMs;
    }
    Serial.print("ISR count=");
    Serial.print(count);
    Serial.print(" last_ms=");
    Serial.println(lastMs);
    printStats("BLINK", blinkStats);
    printStats("BUTTON", buttonStats);
    printStats("CONSOLE", consoleStats);
    Serial.print("ROUNDS total=");
    Serial.print(rounds.rounds);
    Serial.print(" empty=");
    Serial.println(rounds.emptyRounds);
  }
  else if (line[0])
  {
    Serial.println("ERR commands: rate <ms>, pause, stats");
  }
}

static void consoleStep()
{
  static char line[32];
  static size_t used;
  /* Take what is there now; a line split across rounds is fine. */
  while (Serial.available())
  {
    const int c = Serial.read();
    if (c == '\r')
    {
      continue;
    }
    if (c == '\n')
    {
      line[used] = 0;
      runCommand(line);
      used = 0;
    }
    else if (used < sizeof line - 1)
    {
      line[used++] = (char)c;
    }
  }
}

static bool uartHasBytes()
{
  return Serial.available() > 0;
}

static CH32RVTaskKit::Work blink(blinkStep);
static CH32RVTaskKit::Work console(consoleStep);
static CH32RVTaskKit::Work resume(resumeStep);

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);   /* the log and the commands: RX, TX are the pads you wired */
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  blink.wakeEvery(blinkPeriod);
  console.wakeWhen(uartHasBytes);
  resume.wakeOn(pauseEnds);
  blink.measure(blinkStats);
  button.measure(buttonStats);
  console.measure(consoleStats);
  app.measure(rounds);

  app.attach(blink);
  app.attach(button);
  app.attach(console);
  app.attach(resume);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onPress, FALLING);
  Serial.println("ButtonBlinkConsole ready: rate <ms>, pause, stats");
}

void loop()
{
  app.runOnce();   /* the one place the four Works run */
}
