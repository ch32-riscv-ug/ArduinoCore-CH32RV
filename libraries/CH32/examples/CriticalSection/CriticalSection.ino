/* CriticalSection - reading a value an interrupt writes.
 *
 * Wiring: none. The "interrupt" here is SysTick, which the core already runs
 * for millis(); the sketch shows the shape of the problem rather than needing
 * hardware to demonstrate it.
 *
 * The rule: a variable shared with an interrupt must be volatile, and anything
 * wider than one word - or any pair of variables that must agree - has to be
 * read with interrupts off. On a 32-bit part a uint32_t is atomic; a uint64_t
 * and a struct are not.
 */
static volatile uint32_t counter;      /* one word: atomic to read */
static volatile uint32_t seconds;      /* these two must agree with each */
static volatile uint32_t ticks;        /* other, so they are read together */

void setup()
{
    Serial.begin(115200);
    while (!Serial) {
    }
    Serial.println("noInterrupts() does not nest - one interrupts() re-enables");
}

void loop()
{
    /* One word, so no lock is needed. */
    const uint32_t once = counter;

    /* Two values that must be consistent with each other: take both with
     * interrupts off, and do nothing else in there. Printing inside a critical
     * section is the classic mistake - Serial.write() waits for an interrupt
     * that can no longer run. */
    noInterrupts();
    const uint32_t s = seconds;
    const uint32_t t = ticks;
    interrupts();

    Serial.print("counter=");
    Serial.print(once);
    Serial.print(" seconds=");
    Serial.print(s);
    Serial.print(" ticks=");
    Serial.println(t);

    counter++;
    seconds = millis() / 1000ul;
    ticks = millis();
    delay(1000);
}
