/* One real CH32X035 I2C controller talking to the generic development probe.
 *
 * Route 2 makes I2C1 use PC16=SCL / PC17=SDA, which a ConnectionManifest may
 * bind to any probe I2C target.  This sketch deliberately performs one write:
 * the current P4 direct-IDF target has a bounded one-shot receive job.  A
 * complete read/repeated-START suite is added only after the probe has a
 * task-backed receive re-arm and response queue.
 */
#include <Wire.h>

#include "testcmd.h"

static const uint8_t kPeerAddress = 0x42;
#ifndef X035_I2C_PEER_CLOCK
#define X035_I2C_PEER_CLOCK 10000u
#endif

static void run_checks()
{
  const bool route = Wire.setRoute(2);
  tc_check("route2_selected", route);
  if (!route) {
    tc_skip("peer_address_ack", "I2C route 2 unavailable");
    tc_skip("peer_write_accepted", "I2C route 2 unavailable");
    tc_done();
    return;
  }

  Wire.begin();
  Wire.setClock(X035_I2C_PEER_CLOCK);
  Wire.beginTransmission(kPeerAddress);
  const uint8_t payload[] = {0x11, 0x22, 0x33, 0x44};
  const size_t written = Wire.write(payload, sizeof(payload));
  const uint8_t result = Wire.endTransmission();
  tc_checkv("peer_address_ack", result == 0, result);
  tc_checkv("peer_write_accepted", written == sizeof(payload), written);
  Wire.end();
  tc_done();
}

void setup()
{
  tc_begin("x035_i2c_peer");
}

void loop()
{
  const char *cmd = tc_ready();
  if (!cmd) return;
  if (!strcmp(cmd, "RUN")) run_checks();
  else tc_unknown(cmd);
}
