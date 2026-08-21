/* ShiftOut - drive a 74HC595 shift register, one byte at a time.
 *
 * Wiring: three pins to the '595 - data to DS(14), clock to SHCP(11), latch to
 * STCP(12). Tie OE(13) low and MR(10) high. Eight LEDs with resistors on Q0-Q7.
 *
 * shiftOut() is bit-banged, so any three pins work. It is also the reason a
 * '595 is worth knowing: three pins become eight outputs, and the CH32 parts
 * with 20 pins need that trick more than an Uno does.
 */
static const uint8_t DATA_PIN = LED_BUILTIN;   /* change these three */
static const uint8_t CLOCK_PIN = LED_BUILTIN;
static const uint8_t LATCH_PIN = LED_BUILTIN;

void setup()
{
    pinMode(DATA_PIN, OUTPUT);
    pinMode(CLOCK_PIN, OUTPUT);
    pinMode(LATCH_PIN, OUTPUT);
}

static void write595(uint8_t value)
{
    /* The '595 latches on the rising edge of STCP, so hold it low while the
     * byte is clocked in and pulse it afterwards. Without that the outputs
     * flicker through every intermediate pattern. */
    digitalWrite(LATCH_PIN, LOW);
    shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, value);
    digitalWrite(LATCH_PIN, HIGH);
}

void loop()
{
    for (uint8_t bit = 0; bit < 8; bit++) {
        write595((uint8_t)(1u << bit));
        delay(120);
    }
}
