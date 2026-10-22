/* Wiring, operation and expected output: README.md / README.ja.md. */
/* TryLock - a buffer that loop() fills in several steps, and an ISR that
 * must not touch it meanwhile. The ISR cannot wait for loop() (loop() cannot
 * run until the ISR returns), so it tries the lock and, when it is taken,
 * counts a skip and returns.
 * docs: ../../README.ja.md describes the library. */
#include <CH32RVSyncKit.h>

static const int BUTTON_PIN = PA1;   /* a push button to GND: change to your pin */

static CH32RVSyncKit::TryLock<> bufferLock;
static uint8_t buffer[16];
static volatile uint32_t isrWrites = 0;
static volatile uint32_t isrSkips = 0;
static uint8_t filledUpTo = 0;

static void onPress()
{
  CH32RVSyncKit::TryLock<>::Guard g(bufferLock);
  if (!g.locked())
  {
    isrSkips = isrSkips + 1;
    return;
  }
  buffer[0] = 0xEE;   /* the ISR's own mark */
  isrWrites = isrWrites + 1;
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onPress, FALLING);
  Serial.println("TryLock ready");
}

void loop()
{
  /* Fill the buffer 4 bytes per loop(), holding the lock until it is complete:
   * the one case where a lock is kept from one loop() to the next on purpose. */
  if (filledUpTo == 0 && !bufferLock.tryLock())
  {
    return;   /* the ISR has it right now: try again next loop() */
  }
  for (uint8_t i = 0; i < 4; i++)
  {
    buffer[filledUpTo + i] = (uint8_t)(filledUpTo + i);
  }
  filledUpTo += 4;
  if (filledUpTo == sizeof buffer)
  {
    filledUpTo = 0;
    bufferLock.unlock();
    Serial.print("DONE isr_writes=");
    Serial.print(isrWrites);
    Serial.print(" isr_skips=");
    Serial.println(isrSkips);
  }
  delay(100);
}
