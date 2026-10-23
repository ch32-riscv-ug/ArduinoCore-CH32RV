// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <CH32RVTaskKit.h>

static CH32RVTaskKit::Executor app;
static CH32RVSyncKit::Period fast(10), slow(100), report(2000);
static CH32RVTaskKit::WorkStats fastStats, slowStats;
static CH32RVTaskKit::RoundStats rounds;
static volatile uint32_t sink;

static void fastStep()
{
  sink = sink + 1;
}

static void slowStep()
{
  // en: Deliberately slow work shows how one function delays the whole round.
  // ja: 一つの仕事が巡回全体を遅らせる様子を見るため、意図的に遅くする。
  delayMicroseconds(3000);
}

static void reportStep();

static CH32RVTaskKit::Work fastWork(fastStep), slowWork(slowStep), reportWork(reportStep);

static void printStats(const char *name, const CH32RVTaskKit::WorkStats &s)
{
  Serial.print(name);
  Serial.print(" runs=");
  Serial.print(s.runs);
  Serial.print(" avg_us=");
  Serial.print(s.runs ? s.totalUs / s.runs : 0);
  Serial.print(" max_us=");
  Serial.println(s.maxUs);
}

static void reportStep()
{
  printStats("FAST", fastStats);
  printStats("SLOW", slowStats);
  Serial.print("ROUNDS total=");
  Serial.print(rounds.rounds);
  Serial.print(" empty=");
  Serial.print(rounds.emptyRounds);
  Serial.print(" max_us=");
  Serial.println(rounds.maxUs);
  // en: Reset after reporting to start the next measurement interval. / ja: 表示後にリセットして、次の集計区間を始める。
  fastStats.clear();
  slowStats.clear();
  rounds.clear();
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  app.attach(fastWork);
  app.attach(slowWork);
  app.attach(reportWork);
  fastWork.wakeEvery(fast);
  slowWork.wakeEvery(slow);
  reportWork.wakeEvery(report);
  fastWork.measure(fastStats);
  slowWork.measure(slowStats);
  app.measure(rounds);
  Serial.println("WorkStats ready");
}

void loop()
{
  app.runOnce();
}
