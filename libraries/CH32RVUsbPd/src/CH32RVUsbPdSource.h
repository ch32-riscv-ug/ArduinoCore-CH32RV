/* CH32RVUsbPdSource.h - offer voltages to a USB PD sink (source).
 *
 *     #include <CH32RVUsbPd.h>
 *
 *     bool vbus(bool on) { ...switch the 5 V load switch...; return true; }
 *     bool setVoltage(uint16_t mv, uint16_t ma, bool pps) { ...; return true; }
 *
 *     USBPDSource.setRpCurrent(PD_RP_3A0);
 *     USBPDSource.addFixed(5000, 3000);         // the first one must be 5 V fixed
 *     USBPDSource.addFixed(9000, 2000);
 *     USBPDSource.onVbus(vbus);                 // required
 *     USBPDSource.onSetVoltage(setVoltage);     // required
 *     USBPDSource.begin();
 *     void loop() { USBPDSource.maintain(); }
 *
 * Built to be used in a product: the board's supply is the sketch's, reached
 * through the two callbacks, and both are required - begin() fails without
 * them (lastError()). Whether the supply really reaches the voltage is the
 * board's business; the driver trusts the callback's answer. A callback that
 * returns false stops what it was asked for: VBUS stays off (no attach), a
 * voltage change ends in a Hard Reset back to 5 V.
 *
 * What the driver does on its own, following USB PD R3.1 / Type-C 2.x:
 *   - presents Rp on both CC lines (the current set by setRpCurrent()), tells
 *     a sink's Rd from an open line or a cable's Ra, debounces the attach
 *     (tCCDebounce) and selects the CC line it is on
 *   - VBUS on (onVbus(true)), then Source_Capabilities, resent every
 *     tTypeCSendSourceCap until the sink acknowledges one (nCapsCount; a sink
 *     that never does is Type-C only: attached(), not ready())
 *   - checks each Request against what it offered (position, current, the PPS
 *     range): Reject if outside, else Accept, onSetVoltage(), PS_RDY
 *   - Get_Source_Cap: the capabilities again; Soft_Reset: Accept, then them
 *     again; a Hard Reset either way: VBUS off and back on (tSrcRecover)
 *   - a PPS contract the sink stops renewing (tPPSTimeout, 15 s) ends in a
 *     Hard Reset, as the spec asks
 *   - anything it does not implement: Not_Supported (PD 3.0) / Reject (2.0)
 *   - detach (the sink's Rd gone): VBUS off
 *
 * One role at a time: the USBPD block is either this or the sink (USBPD);
 * begin() fails with Busy while the other one runs. Same hardware support as
 * the sink: CH32X035/X033.
 */
#pragma once

#include <stdint.h>

#include "pd_frames.h"

/* The current a source advertises through its Rp before any contract. */
enum PDRpCurrent : uint8_t {
    PD_RP_DEFAULT = 0,       /* Default USB power (500 / 900 mA)  */
    PD_RP_1A5,               /* 1.5 A                             */
    PD_RP_3A0,               /* 3.0 A                             */
};

enum class PDSourceError : uint8_t {
    None = 0,
    Unsupported,             /* no USBPD block brought up on this part        */
    Busy,                    /* the block is a sink right now (USBPD)          */
    NoVbusCallback,          /* onVbus() was not set                           */
    NoVoltageCallback,       /* onSetVoltage() was not set                     */
    NoProfiles,              /* nothing offered                                */
    BadProfiles,             /* the first profile is not 5 V fixed             */
    VbusFailed,              /* the VBUS callback refused to switch on         */
    VoltageFailed,           /* the voltage callback refused a change          */
};

namespace arduino {

class CH32RVUsbPdSource {
public:
    /* on: switch VBUS to vSafe5V (true) or vSafe0V (false). Return true once
     * it is there. */
    typedef bool (*VbusCallback)(bool on);
    /* Bring the output to `mv` with `ma` available (pps: a PPS contract, the
     * voltage in 20 mV steps). Return true once it is there. */
    typedef bool (*VoltageCallback)(uint16_t mv, uint16_t ma, bool pps);

    void setRpCurrent(PDRpCurrent current);
    /* Up to 7 profiles, in the order the sink will see them; the first must
     * be fixed 5 V. False when there is no room or the values do not fit. */
    bool addFixed(uint16_t millivolts, uint16_t milliamps);
    bool addPps(uint16_t minMillivolts, uint16_t maxMillivolts, uint16_t milliamps);
    void clearProfiles();
    /* The "unconstrained power" bit of the 5 V profile: the supply is not
     * shared with anything else (e.g. a wall adapter). */
    void setUnconstrainedPower(bool unconstrained);

    void onVbus(VbusCallback cb);
    void onSetVoltage(VoltageCallback cb);

    bool begin();
    void end();

    /* A sink is attached (VBUS on). */
    bool attached();
    /* An explicit contract is in place. */
    bool ready();

    uint8_t profileCount() const;
    pd_pdo_t profile(uint8_t index) const;
    /* The contract: 5 V with no contract while attached, 0 when detached. */
    uint16_t voltage() const;
    uint16_t current() const;
    int8_t contractProfile() const;
    bool pps() const;

    void maintain();

    PDSourceError lastError() const;
    const char *lastErrorName() const;

    /* For the interrupt. */
    void irq();
};

}  // namespace arduino

extern arduino::CH32RVUsbPdSource USBPDSource;
