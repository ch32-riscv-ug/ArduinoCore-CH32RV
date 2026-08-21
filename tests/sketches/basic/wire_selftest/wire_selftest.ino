/* Wire (I2C) self-check that needs no wiring and no device on the bus.
 *
 * What it can prove without hardware is the half that hangs sketches: that a
 * transfer to nothing on the bus ends, quickly, with an error code, instead of
 * spinning forever. The rest of the checks are pure API behaviour that must
 * hold whatever the bus is doing - buffer overflow reporting, reads from an
 * empty buffer, write() outside a transmission.
 *
 * Talking to a real device is a separate, wired test (docs/todo.ja.md).
 */
#include <Wire.h>

static int failures;

static void check(const char *name, bool ok)
{
    Serial.print(name);
    Serial.println(ok ? " PASS" : " FAIL");
    if (!ok) {
        failures++;
    }
}

/* An address nothing answers. 0x7F is reserved for 10-bit addressing, so no
 * ordinary device is there even when the bench does have a device attached. */
static const uint8_t NOBODY = 0x7F;

void setup()
{
    Serial.begin(115200);
    while (!Serial) {
    }
    delay(50);
    Serial.println("wire_selftest start");

    Wire.begin();

    /* 1. A transmission to nobody has to end, and say it failed. Without
     *    pull-ups the lines never rise and the driver times out (5); with
     *    pull-ups and no device it is an address NACK (2). Both are correct
     *    here - what is being checked is that it returns at all. */
    uint32_t t0 = millis();
    Wire.beginTransmission(NOBODY);
    Wire.write((uint8_t)0x00);
    uint8_t rc = Wire.endTransmission();
    uint32_t elapsed = millis() - t0;
    check("nack_reported", rc != 0);
    check("nack_bounded", elapsed < 200);

    /* 2. Same for a read. */
    t0 = millis();
    size_t got = Wire.requestFrom(NOBODY, (size_t)2);
    elapsed = millis() - t0;
    check("read_reported", got == 0);
    check("read_bounded", elapsed < 200);
    check("read_empty", Wire.available() == 0 && Wire.read() == -1);

    /* 3. More than the buffer holds is reported as 1 and never reaches the
     *    bus - the AVR behaviour libraries check for. */
    Wire.beginTransmission(NOBODY);
    size_t written = 0;
    for (int i = 0; i < CH32_WIRE_BUFFER_SIZE + 4; i++) {
        written += Wire.write((uint8_t)i);
    }
    t0 = millis();
    rc = Wire.endTransmission();
    elapsed = millis() - t0;
    check("overflow_truncates", written == CH32_WIRE_BUFFER_SIZE);
    check("overflow_code", rc == 1);
    check("overflow_skips_bus", elapsed < 5);

    /* 4. write() outside a transmission goes nowhere rather than into the
     *    next transmission's buffer. */
    check("write_outside", Wire.write((uint8_t)0xAA) == 0);

    /* 5. Changing the clock while idle must not wedge the peripheral: the
     *    next transfer still gets to the same error. */
    Wire.setClock(400000);
    Wire.beginTransmission(NOBODY);
    rc = Wire.endTransmission();
    check("fast_mode_still_reports", rc != 0);
    Wire.setClock(100000);

    /* 6. And end()/begin() is a legal cycle. */
    Wire.end();
    Wire.begin();
    Wire.beginTransmission(NOBODY);
    rc = Wire.endTransmission();
    check("restart_still_reports", rc != 0);

    Serial.print("wire_selftest done failures=");
    Serial.println(failures);
}

void loop()
{
}
