/* SPI self-check that needs no wiring.
 *
 * With MISO pulled up and nothing driving it, every transfer reads back 0xFF.
 * That is a weak check of the data path but a strong check of the parts that
 * hang: that a transfer completes at all, at every clock and mode, and that
 * begin/end/beginTransaction can be cycled.
 *
 * Reading back what a device sent is a wired test (docs/todo.ja.md).
 */
#include <SPI.h>

#include "testcmd.h"

static void run_checks()
{
    SPI.begin();

    /* 1. A transfer returns, and with MISO idle high it returns 0xFF. */
    uint32_t t0 = millis();
    uint8_t got = SPI.transfer(0x5A);
    tc_check("transfer_returns", millis() - t0 < 50);
    tc_check("idle_high", got == 0xFF);

    /* 2. Every mode and a spread of clocks, since each one rewrites CTLR1. */
    bool all_modes = true;
    for (uint8_t mode = 0; mode < 4; mode++) {
        SPI.beginTransaction(SPISettings(1000000, MSBFIRST, (SPIMode)mode));
        all_modes = all_modes && SPI.transfer(0x00) == 0xFF;
        SPI.endTransaction();
    }
    tc_check("all_modes", all_modes);

    bool all_clocks = true;
    const uint32_t clocks[] = {125000, 1000000, 4000000, 8000000};
    for (unsigned i = 0; i < sizeof clocks / sizeof clocks[0]; i++) {
        SPI.beginTransaction(SPISettings(clocks[i], MSBFIRST, SPI_MODE0));
        all_clocks = all_clocks && SPI.transfer(0x00) == 0xFF;
        SPI.endTransaction();
    }
    tc_check("all_clocks", all_clocks);

    /* 3. LSB first is a real setting, not a no-op: it still has to transfer. */
    SPI.beginTransaction(SPISettings(1000000, LSBFIRST, SPI_MODE0));
    tc_check("lsb_first", SPI.transfer(0x01) == 0xFF);
    SPI.endTransaction();

    /* 4. Block and 16-bit forms. */
    uint8_t buf[4] = {1, 2, 3, 4};
    SPI.transfer(buf, sizeof buf);
    tc_check("block_transfer", buf[0] == 0xFF && buf[3] == 0xFF);
    tc_check("transfer16", SPI.transfer16(0x1234) == 0xFFFF);

    /* 5. The legacy pre-transaction API still configures the bus. */
    SPI.setClockDivider(SPI_CLOCK_DIV16);
    SPI.setDataMode(SPI_MODE3);
    SPI.setBitOrder(MSBFIRST);
    tc_check("legacy_api", SPI.transfer(0x00) == 0xFF);

    /* 6. end() then begin() is a legal cycle. */
    SPI.end();
    SPI.begin();
    tc_check("restart", SPI.transfer(0x00) == 0xFF);

    tc_done();
}

void setup()
{
    tc_begin("spi_selftest");
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
