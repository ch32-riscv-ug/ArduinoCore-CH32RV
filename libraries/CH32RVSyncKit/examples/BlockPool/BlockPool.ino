// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <CH32RVSyncKit.h>

static const int BUTTON_PIN = PA1;

struct Record
{
  uint32_t ms;
  uint32_t us;
  uint8_t samples[8];
};

static CH32RVSyncKit::BlockPool<Record, 4> pool;
static CH32RVSyncKit::Queue<Record *, 4> filled;
static volatile uint32_t noBlock = 0;

static void onPress()
{
  Record *r = pool.tryAllocate();
  if (!r)
  {
    noBlock = noBlock + 1;
    return;
  }
  r->ms = millis();
  r->us = micros();
  for (uint8_t i = 0; i < sizeof r->samples; i++)
  {
    r->samples[i] = (uint8_t)digitalRead(BUTTON_PIN);
  }
  // en: The queue has one slot per pool block; ownership now passes to the consumer.
  // ja: Queueは全ブロック分の枠を持つ。以後は受け手がブロックを管理する。
  filled.trySend(r);
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onPress, FALLING);
  Serial.println("BlockPool ready");
}

void loop()
{
  Record *r;
  while (filled.tryReceive(r))
  {
    Serial.print("RECORD ms=");
    Serial.print(r->ms);
    Serial.print(" us=");
    Serial.print(r->us);
    Serial.print(" free=");
    // en: Do not access the record after returning it to the pool. / ja: プールへ返した後はこのレコードに触らない。
    pool.release(r);
    Serial.println(pool.available());
  }
  if (noBlock)
  {
    Serial.print("NO_BLOCK ");
    Serial.println(noBlock);
    noBlock = 0;
  }
  delay(1000);
}
