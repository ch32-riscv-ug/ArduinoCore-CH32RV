/* USBPD.h - ask a USB PD source for a voltage (sink).
 *
 *     #include <USBPD.h>
 *
 *     if (USBPD.begin()) {
 *         while (!USBPD.ready()) { }              // charger enumerated
 *         for (uint8_t i = 0; i < USBPD.profileCount(); i++) {
 *             PDProfile p = USBPD.profile(i);     // what the charger offers
 *         }
 *         USBPD.request(9000);                    // 9 V, please
 *     }
 *     void loop() { USBPD.maintain(); }           // keeps the link (and a PPS contract) alive
 *
 * Every call - ready(), connected(), request(), maintain() - moves the driver
 * along: attach detection, the spec's timers, retransmission and the PPS
 * keepalive all run from whichever of them the sketch calls, so waiting on
 * ready() alone is enough to reach a contract.
 *
 * VERIFIED against real chargers (2026-10-02, CH32X035 on a WeAct board): a
 * 5/9/12/15/20 V fixed supply and a 5/9/12 V + 3.3-11 V PPS one - attach,
 * Source_Capabilities, the driver's own 5 V contract, every fixed level and
 * back, PPS at both ends and the middle of its range held 20 s on maintain()
 * alone, refusals, and a restart under a live contract
 * (tests/bench/basic/pd_sink). Implemented for
 * CH32X035/X033; begin() returns false on the other series with the block,
 * whose defines wait on the next device-data adoption (see usbpd_hw.h).
 *
 * What the driver does on its own, following USB PD R3.1:
 *
 *   - answers Source_Capabilities with a Request: for the profile the sketch
 *     last obtained if the same charger still offers that very PDO, else
 *     profile 0 (5 V); a detach forgets the choice -
 *     the spec requires a Request, so silence is not an option
 *   - starting late (a reset, a re-flash) while the charger already holds a
 *     contract: no Source_Capabilities arrive, so after tTypeCSinkWaitCap it
 *     sends Soft_Reset (VBUS stays up), then Hard Reset (VBUS drops and comes
 *     back at 5 V - a board powered only from VBUS restarts), at most twice;
 *     a charger that never answers is Type-C only: connected(), not ready()
 *   - retransmits a message the charger did not GoodCRC (twice), ignores a
 *     message the charger repeated, and follows the response timers
 *     (Accept 30 ms, PS_RDY 550 ms) with a Soft/Hard Reset
 *   - answers Get_Sink_Cap with a 5 V Sink_Capabilities, and anything it does
 *     not implement with Not_Supported (PD 3.0) or Reject (PD 2.0)
 *   - re-requests a PPS contract every 5 s (the charger drops one that goes
 *     quiet for 10 s), as long as the sketch calls maintain() - a sketch
 *     holding a PPS contract cannot sit in delay(30000); a fixed one can
 *   - notices the charger going away when CC falls below vRd-Connect (only
 *     meaningful on a board not powered from that same VBUS)
 *
 * Voltages are millivolts, currents milliamps, everywhere. request(9)
 * asking for 9 mV instead of 9 V fails cleanly: no profile offers it.
 *
 * The USBPD block exists on seven series (X035/X033, L103/M103, V205, X315,
 * H417, M030 - two register placements, per-series CC pads), so this library
 * is not X035-specific; the variant will say where its block lives once the
 * defines are generated (blocked on the next device-data adoption).
 *
 * Board note: keep the CC lines short. A bench lead from CC to a probe pin
 * (measured on the WeAct board) loads them enough that no message decodes.
 */
#pragma once

#include <stdint.h>

#include "pd_frames.h"
/* Where (and whether) this part's USBPD block lives. Included here so a
 * sketch can feature-test the same way the driver does:
 *     #ifdef CH32RV_USBPD_BASE
 */
#include "usbpd_hw.h"

/* One entry of the source's advertisement, as Arduino code sees it.
 * Field names carry their unit on purpose: p.max_mv cannot be misread the
 * way p.maxVoltage can. min_mv == max_mv for a fixed profile. */
typedef pd_pdo_t PDProfile;

namespace arduino {

class CH32RVUsbPd {
public:
    /* Start CC detection. False when this part has no USBPD block brought up
     * (see usbpd_hw.h). */
    bool begin();
    void end();

    /* A source is attached (CC shows its pull-up). True as well for a
     * Type-C charger that never speaks PD - ready() tells the two apart. */
    bool connected();

    /* An explicit contract is in place (the source sent PS_RDY), so the
     * profiles below describe this charger and voltage()/current() are live.
     * False while a renegotiation is under way. */
    bool ready();

    /* The source's advertisement, in the order it was sent. Index 0 is the
     * 5 V fixed profile the spec requires every source to list first. */
    uint8_t profileCount() const;
    PDProfile profile(uint8_t index) const;

    /* Ask for a voltage, and wait (up to about 1.5 s) for the answer. Fixed
     * profiles match exactly; a PPS profile takes anything inside its range
     * (20 mV steps, truncated). milliamps = 0 means "whatever the profile
     * offers". A fixed level wins over a PPS range holding the same voltage.
     * False when nothing fits or the source refused - the previous contract
     * then stands. request(8000) on a 5/9/12 V charger without PPS is false,
     * not "9 V is close enough". */
    bool request(uint16_t millivolts, uint16_t milliamps = 0);

    /* The same, naming the profile - for a PPS range when a fixed level also
     * matches, or a specific entry from a listing. millivolts is required for
     * PPS profiles and ignored for fixed ones. */
    bool requestProfile(uint8_t index, uint16_t millivolts = 0,
                        uint16_t milliamps = 0);

    /* What the contract says - a promise, not a measurement. 0 before
     * ready(). After a successful request(): the values the source accepted,
     * in the Request's own steps (PPS: 20 mV / 50 mA, truncated). */
    uint16_t voltage() const;
    uint16_t current() const;

    /* The profile the contract is on (an index for profile()), -1 without one. */
    int8_t contractProfile() const;

    /* The contract is on a PPS profile (and so needs maintain()). */
    bool pps() const;

    /* The driver's heartbeat: attach and detach detection, the spec's timers,
     * retransmission, and the PPS keepalive. Call it from loop(). */
    void maintain();

    /* Interrupt entry point for the USBPD vector. Public the way
     * HardwareSerial::irq() is; not part of the sketch-facing API. */
    void irq();
};

}  // namespace arduino

extern arduino::CH32RVUsbPd USBPD;
