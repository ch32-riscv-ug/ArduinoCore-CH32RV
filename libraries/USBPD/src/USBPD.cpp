/* The Arduino face of the PD sink. The decisions live in pd_frames.c where
 * tests reach them; this file only holds state and, once written, the
 * hardware driver. See USBPD.h for what is and is not implemented. */
#include "USBPD.h"

namespace arduino {

bool CH32UsbPd::begin()
{
    _caps.count = 0;
    _contract_mv = 0;
    _contract_ma = 0;
    /* No hardware driver yet (see USBPD.h). Saying so beats pretending. */
    return false;
}

void CH32UsbPd::end()
{
    _caps.count = 0;
    _contract_mv = 0;
    _contract_ma = 0;
}

bool CH32UsbPd::connected() { return false; }
bool CH32UsbPd::ready()     { return false; }

PDProfile CH32UsbPd::profile(uint8_t index) const
{
    if (index >= _caps.count) {
        /* Out of range reads as "no supply" rather than as stack garbage. */
        PDProfile none = {PD_SUPPLY_UNKNOWN, 0, 0, 0, 0, 0};
        return none;
    }
    return _caps.pdo[index];
}

bool CH32UsbPd::request(uint16_t millivolts, uint16_t milliamps)
{
    return requestProfile((uint8_t)pd_pick(&_caps, millivolts, milliamps),
                          millivolts, milliamps);
}

bool CH32UsbPd::requestProfile(uint8_t index, uint16_t millivolts,
                               uint16_t milliamps)
{
    /* pd_pick() returns -1 as 0xFF through the cast above; pd_request_for
     * rejects it with everything else that cannot be asked for. */
    const uint32_t rdo = pd_request_for(&_caps, index >= _caps.count ? -1 : index,
                                        millivolts, milliamps);
    if (rdo == 0u) {
        return false;
    }
    /* Driver not written yet: nothing to send the RDO with. */
    return false;
}

void CH32UsbPd::maintain() {}

}  // namespace arduino

arduino::CH32UsbPd USBPD;
