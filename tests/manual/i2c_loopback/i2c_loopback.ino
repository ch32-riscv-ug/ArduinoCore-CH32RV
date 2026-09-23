/* Wire's slave mode against Wire's master mode, on one board.
 *
 * I2C1 is the master (Wire) and I2C2 the slave (Wire1), joined by two
 * jumpers and pulled up. One part, both roles: the data path, the callbacks,
 * the over-read filler and the buffer cap are all observable without a
 * second board or a logic analyzer.
 *
 * Needs a series that bonds both buses - V103/V203/L103 do (I2C1 PB6/PB7,
 * I2C2 PB10/PB11); X035 has only I2C1, so this sketch reports SKIPs there.
 */
#include <Wire.h>

#include "testcmd.h"

static const uint8_t SLAVE = 0x2A;
static const uint8_t NOBODY = 0x2B;   /* one off, so a stuck ACK is caught */

#if defined(CH32_I2C1_SCL) && defined(CH32_I2C2_SCL)

static volatile int rx_events;
static volatile int rx_last_count;
static uint8_t rx_copy[CH32_WIRE_BUFFER_SIZE];
static volatile uint8_t rx_copy_len;

static void on_receive(int count)
{
    rx_events++;
    rx_last_count = count;
    rx_copy_len = 0;
    while (Wire1.available() && rx_copy_len < sizeof rx_copy) {
        rx_copy[rx_copy_len++] = (uint8_t)Wire1.read();
    }
}

static const uint8_t REPLY[3] = {0x5A, 0xC3, 0x7E};

static void on_request(void)
{
    Wire1.write(REPLY, sizeof REPLY);
}

static void run_checks()
{
    rx_events = 0;

    Wire1.onReceive(on_receive);
    Wire1.onRequest(on_request);
    Wire1.begin(SLAVE);
    Wire.begin();

    /* 1. The address is acknowledged through the wire - and only that
     *    address, or this would just be a shorted bus saying yes. */
    Wire.beginTransmission(SLAVE);
    uint8_t rc = Wire.endTransmission();
    tc_checkv("slave_acks_address", rc == 0, rc);
    Wire.beginTransmission(NOBODY);
    rc = Wire.endTransmission();
    tc_checkv("other_address_nacked", rc == 2, rc);

    /* 2. Master to slave: the bytes arrive, once, with the right count. */
    static const uint8_t PING[4] = {0x11, 0x22, 0x33, 0x44};
    rx_events = 0;
    Wire.beginTransmission(SLAVE);
    Wire.write(PING, sizeof PING);
    const uint8_t wrc = Wire.endTransmission();
    delay(2);                          /* the STOPF interrupt is not instant */
    tc_checkv("write_delivered", wrc == 0, wrc);
    tc_checkv("receive_event_once", rx_events == 1, rx_events);
    tc_checkv("receive_count", rx_last_count == (int)sizeof PING,
              rx_last_count);
    bool same = rx_copy_len == sizeof PING;
    for (uint8_t i = 0; same && i < sizeof PING; i++) {
        same = rx_copy[i] == PING[i];
    }
    tc_check("receive_bytes", same);

    /* 3. Slave to master: what onRequest() wrote is what arrives. */
    size_t got = Wire.requestFrom(SLAVE, (size_t)sizeof REPLY);
    bool reply_ok = got == sizeof REPLY;
    for (uint8_t i = 0; reply_ok && i < sizeof REPLY; i++) {
        reply_ok = Wire.read() == REPLY[i];
    }
    tc_checkv("request_reply", reply_ok, (long)got);

    /* 4. Reading past what the slave offered yields the 0xFF filler, and
     *    the transfer still ends cleanly. */
    got = Wire.requestFrom(SLAVE, (size_t)(sizeof REPLY + 2));
    bool filler_ok = got == sizeof REPLY + 2;
    for (uint8_t i = 0; filler_ok && i < sizeof REPLY; i++) {
        filler_ok = Wire.read() == REPLY[i];
    }
    filler_ok = filler_ok && Wire.read() == 0xFF && Wire.read() == 0xFF;
    tc_check("overread_gets_ff", filler_ok);

    /* 5. A message longer than the slave's buffer keeps its head: the head
     *    is intact and the reported count stops at the cap. */
    rx_events = 0;
    Wire.beginTransmission(SLAVE);
    for (uint8_t i = 0; i < CH32_WIRE_BUFFER_SIZE; i++) {
        Wire.write(i);
    }
    const uint8_t full_rc = Wire.endTransmission();
    delay(2);
    bool head = full_rc == 0 && rx_events == 1
                && rx_last_count == CH32_WIRE_BUFFER_SIZE
                && rx_copy_len == CH32_WIRE_BUFFER_SIZE;
    for (uint8_t i = 0; head && i < CH32_WIRE_BUFFER_SIZE; i++) {
        head = rx_copy[i] == i;
    }
    tc_checkv("full_buffer_delivered", head, rx_last_count);

    /* 6. And again, to prove nothing above wedged either side. */
    rx_events = 0;
    Wire.beginTransmission(SLAVE);
    Wire.write((uint8_t)0x99);
    const uint8_t again = Wire.endTransmission();
    delay(2);
    tc_checkv("second_round_works",
              again == 0 && rx_events == 1 && rx_last_count == 1
              && rx_copy[0] == 0x99, again);

    Wire.end();
    Wire1.end();
    tc_done();
}

#else  /* one bus only on this series */

static void run_checks()
{
    static const char *const WHY = "one I2C bus only on this series";
    tc_skip("slave_acks_address", WHY);
    tc_skip("other_address_nacked", WHY);
    tc_skip("write_delivered", WHY);
    tc_skip("receive_event_once", WHY);
    tc_skip("receive_count", WHY);
    tc_skip("receive_bytes", WHY);
    tc_skip("request_reply", WHY);
    tc_skip("overread_gets_ff", WHY);
    tc_skip("full_buffer_delivered", WHY);
    tc_skip("second_round_works", WHY);
    tc_done();
}

#endif

void setup()
{
    tc_begin("i2c_loopback");
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
