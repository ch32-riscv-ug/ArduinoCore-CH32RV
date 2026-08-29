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

/* Counted, not acted on: with nothing wired to the bus, any callback firing
 * is the driver inventing traffic. */
static volatile int slave_rx_events;
static volatile int slave_req_events;

static void on_slave_receive(int n)
{
    (void)n;
    slave_rx_events++;
}

static void on_slave_request(void)
{
    slave_req_events++;
}

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

    /* 5b. The AVR-compatible timeout API. There is no way to *make* the bus
     *     hang from here without wiring, so what is checked is what can be:
     *     the flag round-trips, a NACK does not raise it (a NACK is not a
     *     timeout), and turning the timeout off and back on leaves the bus
     *     usable. Disabling it is only safe here because nothing on this
     *     board holds SCL low. */
    Wire.clearWireTimeoutFlag();
    tc_check("timeout_flag_clears", Wire.getWireTimeoutFlag() == false);
    Wire.beginTransmission(NOBODY);
    rc = Wire.endTransmission();
    tc_check("nack_is_not_a_timeout",
             rc != 0 && Wire.getWireTimeoutFlag() == false);
    Wire.setWireTimeout(0);
    Wire.beginTransmission(NOBODY);
    rc = Wire.endTransmission();
    tc_check("no_timeout_still_reports", rc != 0);
    Wire.setWireTimeout();
    Wire.beginTransmission(NOBODY);
    rc = Wire.endTransmission();
    tc_check("timeout_restored", rc != 0);

    /* 6. And end()/begin() is a legal cycle. */
    Wire.end();
    Wire.begin();
    Wire.beginTransmission(NOBODY);
    rc = Wire.endTransmission();
    tc_check("restart_still_reports", rc != 0);

    /* 7. Slave mode, as far as it can be seen without a master on the bus:
     *    it comes up, it does not invent traffic, master calls are refused
     *    while it holds the peripheral, and the way back to master mode
     *    works. The actual data path needs two buses wired together -
     *    manual/i2c_loopback. */
    slave_rx_events = 0;
    slave_req_events = 0;
    Wire.end();
    Wire.onReceive(on_slave_receive);
    Wire.onRequest(on_slave_request);
    Wire.begin(0x2A);
    tc_check("slave_accepts_no_master_calls",
             (Wire.beginTransmission(NOBODY),
              Wire.endTransmission()) == 4
             && Wire.requestFrom(NOBODY, (size_t)2) == 0);
    delay(60);
    tc_checkv("slave_quiet_unwired",
              slave_rx_events == 0 && slave_req_events == 0
              && Wire.available() == 0,
              slave_rx_events * 100 + slave_req_events);
    Wire.end();
    Wire.begin();
    Wire.beginTransmission(NOBODY);
    rc = Wire.endTransmission();
    tc_check("master_after_slave", rc != 0 && rc != 4);

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
