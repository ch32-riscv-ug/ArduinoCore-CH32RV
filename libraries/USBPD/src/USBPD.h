/* USBPD.h - ask a USB PD source for a voltage (sink).
 *
 *     #include <USBPD.h>
 *
 *     if (USBPD.begin()) {
 *         while (!USBPD.ready()) { }              // source enumerated
 *         for (uint8_t i = 0; i < USBPD.profileCount(); i++) {
 *             PDProfile p = USBPD.profile(i);     // what the charger offers
 *         }
 *         USBPD.request(9000);                    // 9 V, please
 *     }
 *     void loop() { USBPD.maintain(); }           // keeps a PPS contract fed
 *
 * WHAT WORKS TODAY: the frame logic - parsing Source_Capabilities, choosing
 * a profile, building Request objects - is implemented and tested on host
 * and on target (tests/unit/test_pd_frames.py, tests/sketches/basic/
 * pd_selftest). The hardware driver (CC detection, the BMC engine, GoodCRC,
 * the sink state machine) is implemented for CH32X035/X033, where the
 * USBPD block's plumbing has been checked; begin() still returns false on
 * the other five series with the block, whose defines wait on the next
 * device-data adoption (see usbpd_hw.h).
 *
 * NOT YET VERIFIED AGAINST A REAL SOURCE: everything up to "an empty port
 * reports disconnected" runs on the bench, but no PD supply has been
 * attached yet, so negotiation itself is untested. Simplifications this
 * first driver makes, deliberately and visibly:
 *
 *   - on attach it requests profile 0 (the mandatory 5 V one) itself: the
 *     spec requires a Request in answer to Source_Capabilities, so doing
 *     nothing until the sketch asks is not an option
 *   - no retransmission: a lost GoodCRC leads to the source's own recovery
 *     (hard reset), which this driver survives and renegotiates through
 *   - detach is not detected (that needs VBUS sensing): connected() latches
 *     until a hard reset, end(), or reboot
 *   - messages a plain sink can ignore are ignored; Soft_Reset is honoured
 *
 * Voltages are millivolts, currents milliamps, everywhere. request(9)
 * asking for 9 mV instead of 9 V fails cleanly: no profile offers it.
 *
 * The USBPD block exists on seven series (X035/X033, L103/M103, V205, X315,
 * H417, M030 - two register placements, per-series CC pads), so this library
 * is not X035-specific; the variant will say where its block lives once the
 * defines are generated (blocked on the next device-data adoption).
 *
 * A PPS contract is not fire-and-forget: the source drops it unless the sink
 * re-requests every few seconds (SinkPPSPeriodicTimer, 10 s ceiling), so a
 * sketch holding one must keep loop() calling maintain() and cannot sit in
 * delay(30000). A fixed contract holds on its own - which is why request()
 * prefers a fixed profile at the exact voltage over a PPS range containing
 * it (see pd_pick in pd_frames.h).
 */
#pragma once

#include <stdint.h>

#include "pd_frames.h"
/* Where (and whether) this part's USBPD block lives. Included here so a
 * sketch can feature-test the same way the driver does:
 *     #ifdef CH32_USBPD_BASE
 */
#include "usbpd_hw.h"

/* One entry of the source's advertisement, as Arduino code sees it.
 * Field names carry their unit on purpose: p.max_mv cannot be misread the
 * way p.maxVoltage can. min_mv == max_mv for a fixed profile. */
typedef pd_pdo_t PDProfile;

namespace arduino {

class CH32UsbPd {
public:
    /* Start CC detection and capability capture. False when this part has no
     * USBPD block - or, today, always: see the header comment. A sketch that
     * checks the return value keeps working the day the driver lands. */
    bool begin();
    void end();

    /* A source is attached (CC detected). */
    bool connected();

    /* An explicit contract is in place (the source sent PS_RDY), so the
     * profiles below describe this charger and voltage()/current() are live. */
    bool ready();

    /* The source's advertisement, in the order it was sent. Index 0 is the
     * 5 V fixed profile the spec requires every source to list first. */
    uint8_t profileCount() const;
    PDProfile profile(uint8_t index) const;

    /* Ask for a voltage. Fixed profiles match exactly; a PPS profile takes
     * anything inside its range (20 mV steps, truncated). milliamps = 0
     * means "whatever the profile offers". False when nothing fits or the
     * source refused. request(8000) on a 5/9/12 V charger without PPS is
     * false, not "9 V is close enough". */
    bool request(uint16_t millivolts, uint16_t milliamps = 0);

    /* The same, naming the profile - for forcing a PPS range when a fixed
     * level also matches, or driving a specific entry from a listing.
     * millivolts is required for PPS profiles and ignored for fixed ones. */
    bool requestProfile(uint8_t index, uint16_t millivolts = 0,
                        uint16_t milliamps = 0);

    /* What the contract says - a promise, not a measurement. 0 before
     * ready(). After a successful request(): the values the source accepted. */
    uint16_t voltage() const;
    uint16_t current() const;

    /* The driver's heartbeat: attach polling while disconnected, and the
     * re-request that keeps a PPS contract alive. Call it from loop(). */
    void maintain();

    /* Interrupt entry point for the USBPD vector. Public the way
     * HardwareSerial::irq() is; not part of the sketch-facing API. */
    void irq();
};

}  // namespace arduino

extern arduino::CH32UsbPd USBPD;
