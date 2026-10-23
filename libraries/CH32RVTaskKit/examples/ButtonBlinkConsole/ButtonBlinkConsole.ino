// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <CH32RVTaskKit.h>
#include <stdlib.h>
#include <string.h>

static const int LED_PIN = PC0;
static const int BUTTON_PIN = PA1;

static CH32RVTaskKit::Executor app;

static CH32RVSyncKit::Queue<uint32_t, 8> presses;
static volatile uint32_t isrCount;
static volatile uint32_t isrLastMs;

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

static CH32RVSyncKit::Deadline pauseEnds;

static void resumeStep()
{
  if (!pauseEnds.expired())
  {
    return;
  }
  // en: Disarm the expired deadline so resume happens once per pause. / ja: 期限を解除し、一回のpauseで一度だけ再開する。
  pauseEnds.disarm();
  paused = false;
  Serial.println("RESUME");
}

static void buttonStep()
{
  static uint32_t lastAccepted;
  uint32_t t;
  while (presses.tryReceive(t))
  {
    // en: Debounce queued timestamps rather than delaying inside the ISR. / ja: ISRで待たず、Queueの時刻でチャタリングを除く。
    if (t - lastAccepted < 50)
    {
      continue;
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
  // en: Queue carries data; post() requests execution and may coalesce.
  // ja: データはQueueへ。post()は実行の合図で、複数回が一回にまとまる。
  presses.trySend(millis());
  isrCount = isrCount + 1;
  isrLastMs = millis();
  button.post();
}

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
      // en: Copy this ISR-written pair together; print only after restoring interrupts.
      // ja: ISRが書く二値をまとめてコピーし、割り込みを戻してから表示する。
      CH32RVSyncKit::CriticalSection cs;
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
  // en: Keep partial lines across work invocations. / ja: 行の途中までの入力を、仕事の呼び出しをまたいで保持する。
  static char line[32];
  static size_t used;

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
  Serial.begin(115200, SERIAL_8N1, RX, TX);
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
  // en: All four work functions execute here, not in the ISR. / ja: 四つの仕事はISRではなくここで動く。
  app.runOnce();
}
