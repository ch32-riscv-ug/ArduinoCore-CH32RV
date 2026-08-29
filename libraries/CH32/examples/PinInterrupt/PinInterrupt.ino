/* PinInterrupt - count edges on a pin without polling for them.
 *
 * Wiring: a button between the pin and ground. The pin is pulled up, so it
 * reads high until the button is pressed. Without a button the input still
 * works - touching the pad with a finger is usually enough to see counts.
 *
 * EXTI lines are numbered by the pin's bit rather than by its port, so two
 * pads with the same bit number (PA3 and PB3) share one line and cannot both
 * have an interrupt. attachInterrupt() on the second one replaces the first.
 */
/* Change to your button's pin. Any pad with an EXTI line works, which is all
 * of them; PC0 is just a pad that exists on the series this example is built
 * for. */
static const uint8_t BUTTON = PC0;

static volatile uint32_t edges;

/* Interrupt handlers must be short and must not print: Serial.write() blocks
 * on a full buffer, and the buffer is drained by another interrupt. */
static void on_edge()
{
    edges++;
}

void setup()
{
    Serial.begin(115200);
    while (!Serial) {
    }
    pinMode(BUTTON, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BUTTON), on_edge, FALLING);
    Serial.println("counting falling edges");
}

void loop()
{
    /* Copy once: the handler can fire between two reads. */
    const uint32_t now = edges;
    static uint32_t shown;
    if (now != shown) {
        shown = now;
        Serial.print("edges: ");
        Serial.println(now);
    }
    delay(50);
}
