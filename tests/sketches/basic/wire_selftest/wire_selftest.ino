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

#include "testcmd.h"

/* An address nothing answers. 0x7F is reserved for 10-bit addressing, so no
 * ordinary device is there even when the bench does have a device attached. */
static const uint8_t NOBODY = 0x7F;

static void run_checks()
{
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
    tc_check("nack_reported", rc != 0);
    tc_check("nack_bounded", elapsed < 200);

    /* 2. Same for a read. */
    t0 = millis();
    size_t got = Wire.requestFrom(NOBODY, (size_t)2);
    elapsed = millis() - t0;
    tc_check("read_reported", got == 0);
    tc_check("read_bounded", elapsed < 200);
    tc_check("read_empty", Wire.available() == 0 && Wire.read() == -1);

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
    tc_check("overflow_truncates", written == CH32_WIRE_BUFFER_SIZE);
    tc_check("overflow_code", rc == 1);
    tc_check("overflow_skips_bus", elapsed < 5);

    /* 4. write() outside a transmission goes nowhere rather than into the
     *    next transmission's buffer. */
    tc_check("write_outside", Wire.write((uint8_t)0xAA) == 0);

    /* 5. Changing the clock while idle must not wedge the peripheral: the
     *    next transfer still gets to the same error. */
    Wire.setClock(400000);
    Wire.beginTransmission(NOBODY);
    rc = Wire.endTransmission();
    tc_check("fast_mode_still_reports", rc != 0);
    Wire.setClock(100000);

    /* 6. And end()/begin() is a legal cycle. */
    Wire.end();
    Wire.begin();
    Wire.beginTransmission(NOBODY);
    rc = Wire.endTransmission();
    tc_check("restart_still_reports", rc != 0);

    tc_done();
}

void setup()
{
    tc_begin("wire_selftest");
}

void loop()
{
    const char *cmd = tc_ready();
    if (!cmd) {
        return;
    }
    if (!strcmp(cmd, "RUN")) {
        run_checks();
    } else {
        tc_unknown(cmd);
    }
}
