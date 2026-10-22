/* Wiring, operation and expected output: README.md / README.ja.md. */
/* BlockPool - records too big to copy around: the ISR borrows a block from
 * a pool, fills it and sends its pointer; loop() uses it and gives it back.
 * When every block is out, the ISR is told so and skips the record.
 * docs: ../../README.ja.md describes the library. */
#include <CH32RVSyncKit.h>

static const int BUTTON_PIN = PA1;   /* a push button to GND: change to your pin */

struct Record
{
  uint32_t ms;
  uint32_t us;
  uint8_t samples[8];
};

static CH32RVSyncKit::BlockPool<Record, 4> pool;
static CH32RVSyncKit::Queue<Record *, 4> filled;   /* as many places as blocks */
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
  filled.trySend(r);   /* cannot fail: the queue has a place for every block */
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
    pool.release(r);   /* give it back, or the pool runs dry */
    Serial.println(pool.available());
  }
  if (noBlock)
  {
    Serial.print("NO_BLOCK ");
    Serial.println(noBlock);
    noBlock = 0;
  }
  delay(1000);   /* slow on purpose: press quickly to use up the pool */
}
