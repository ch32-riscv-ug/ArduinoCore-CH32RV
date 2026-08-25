/* The USB PD frame logic, on the target, with no PD hardware.
 *
 * libraries/USBPD keeps its protocol logic in pure functions precisely so it
 * can be verified without a charger: tests/unit/test_pd_frames.py covers it
 * on the host, and this sketch re-runs the same vectors on the target. What
 * this adds over the host run is the architecture - rv32ec has no trouble
 * that x86 politely hides, until it does (the 32-bit timer registers were
 * exactly such a case) - so the words below are the same ones the host test
 * derives from the spec's field layouts, here as the raw values.
 *
 * Runs on every board, CH32V003 included: nothing here touches a register.
 */
#include <USBPD.h>

#include "testcmd.h"

/* A typical 65 W PPS charger. Same table as the host test's CHARGER. */
static const uint32_t CHARGER[] = {
    0x2C01912Cu,   /* [0] fixed  5 V 3 A, DRP|unconstrained|USB-comm */
    0x0002D12Cu,   /* [1] fixed  9 V 3 A                             */
    0x0003C12Cu,   /* [2] fixed 12 V 3 A                             */
    0x0004B12Cu,   /* [3] fixed 15 V 3 A                             */
    0x00064145u,   /* [4] fixed 20 V 3.25 A                          */
    0xC0DC2164u,   /* [5] PPS 3.3-11 V 5 A                           */
    0xC1A42164u,   /* [6] PPS 3.3-21 V 5 A                           */
};

static void run_checks()
{
    pd_caps_t caps;
    pd_parse_source_caps(CHARGER, 7, &caps);

    tc_checkv("parse_count", caps.count == 7, caps.count);

    bool kinds = true;
    for (uint8_t i = 0; i < 5; i++) {
        kinds = kinds && caps.pdo[i].kind == PD_SUPPLY_FIXED;
    }
    kinds = kinds && caps.pdo[5].kind == PD_SUPPLY_PPS
                  && caps.pdo[6].kind == PD_SUPPLY_PPS;
    tc_check("kinds_in_order", kinds);

    tc_check("fixed_9v", caps.pdo[1].max_mv == 9000
                         && caps.pdo[1].min_mv == 9000
                         && caps.pdo[1].max_ma == 3000);
    tc_check("pps_low_range", caps.pdo[5].min_mv == 3300
                              && caps.pdo[5].max_mv == 11000
                              && caps.pdo[5].max_ma == 5000);
    tc_check("first_pdo_flags", caps.dual_role_power && caps.unconstrained
                                && caps.usb_comm);

    /* Battery and variable parse; an AVS APDO is unknown, not misread. */
    static const uint32_t ODD[] = {0x2C01912Cu, 0x5A417CB4u, 0x9A41912Cu,
                                   0xD0123456u};
    pd_caps_t odd;
    pd_parse_source_caps(ODD, 4, &odd);
    tc_check("battery_parses", odd.pdo[1].kind == PD_SUPPLY_BATTERY
                               && odd.pdo[1].min_mv == 4750
                               && odd.pdo[1].max_mv == 21000
                               && odd.pdo[1].max_mw == 45000ul);
    tc_check("variable_parses", odd.pdo[2].kind == PD_SUPPLY_VARIABLE
                                && odd.pdo[2].max_ma == 3000);
    tc_check("avs_is_unknown", odd.pdo[3].kind == PD_SUPPLY_UNKNOWN
                               && odd.pdo[3].max_mv == 0);
    tc_check("odd_never_requested",
             pd_pick(&odd, 10000, 0) == -1
             && pd_request_for(&odd, 1, 10000, 0) == 0u
             && pd_request_for(&odd, 3, 5000, 0) == 0u);

    /* Choice policy. */
    tc_checkv("pick_fixed_exact", pd_pick(&caps, 9000, 0) == 1,
              pd_pick(&caps, 9000, 0));
    tc_checkv("pick_pps_between", pd_pick(&caps, 5900, 0) == 5,
              pd_pick(&caps, 5900, 0));
    tc_checkv("pick_pps_current", pd_pick(&caps, 9000, 4000) == 5,
              pd_pick(&caps, 9000, 4000));
    tc_check("pick_refuses", pd_pick(&caps, 40000, 0) == -1
                             && pd_pick(&caps, 9000, 9000) == -1);

    /* Request words, bit for bit. */
    tc_checkv("rdo_fixed", pd_request_for(&caps, 1, 9000, 0) == 0x2004B12Cu,
              (long)pd_request_for(&caps, 1, 9000, 0));
    tc_checkv("rdo_pps", pd_request_for(&caps, 5, 5900, 2000) == 0x60024E28u,
              (long)pd_request_for(&caps, 5, 5900, 2000));
    tc_check("rdo_pps_truncates",
             pd_request_pps(6, 5905, 2000) == pd_request_pps(6, 5900, 2000));
    tc_check("request_caps_current",
             (pd_request_for(&caps, 1, 9000, 0) & 0x3FFu) == 300u);

    /* Header build and read-back. */
    const uint16_t h = pd_header(PD_DATA_REQUEST, 1, 3, PD_REV_3_0);
    tc_checkv("header_fields", h == 0x1682u
                               && pd_header_type(h) == PD_DATA_REQUEST
                               && pd_header_count(h) == 1
                               && pd_header_id(h) == 3
                               && !pd_header_extended(h), h);

    /* The hardware driver, as far as an empty port can prove it: the block
     * comes up, an unplugged port stays "not connected" through real attach
     * polling, a request with no source is refused, and end()/begin() is a
     * legal cycle. Negotiation itself needs a PD supply on the connector.
     * Parts without the block (or not brought up - see usbpd_hw.h) take the
     * SKIP branch, which also proves begin() is honest there. */
#ifdef CH32_USBPD_BASE
    tc_check("hw_begin", USBPD.begin());
    const uint32_t t0 = millis();
    while (millis() - t0 < 300u) {
        USBPD.maintain();
    }
    tc_checkv("hw_unplugged_not_connected",
              !USBPD.connected() && !USBPD.ready(), USBPD.connected());
    tc_check("hw_no_caps", USBPD.profileCount() == 0
                           && USBPD.voltage() == 0);
    tc_check("hw_request_refused", !USBPD.request(9000));
    USBPD.end();
    tc_check("hw_restart", USBPD.begin());
    USBPD.end();
#else
    static const char *const WHY = "no USBPD block brought up on this part";
    tc_skip("hw_begin", WHY);
    tc_check("hw_begin_honest", !USBPD.begin());
    tc_skip("hw_unplugged_not_connected", WHY);
    tc_skip("hw_no_caps", WHY);
    tc_skip("hw_request_refused", WHY);
    tc_skip("hw_restart", WHY);
#endif

    tc_done();
}

void setup()
{
    tc_begin("pd_selftest");
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
