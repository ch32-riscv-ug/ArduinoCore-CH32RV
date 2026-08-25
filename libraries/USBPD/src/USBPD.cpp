/* The USB PD sink driver. Frame decisions live in pd_frames.c where the
 * tests reach them; this file owns the USBPD block - CC detection, the BMC
 * transmitter/receiver, GoodCRC, and the sink state machine. See USBPD.h
 * for what is verified and what still waits for a real PD supply.
 *
 * The flow, from the wire's point of view:
 *
 *   attach      maintain() polls the CC comparators (~10 ms) until one line
 *               shows a source's pull-up, then picks that line and listens
 *   caps        the source sends Source_Capabilities; the interrupt handler
 *               GoodCRCs it, parses it, and answers with a Request for
 *               profile 0 (5 V) - the spec demands *a* Request, so the safe
 *               one goes out before the sketch has said anything
 *   contract    Accept then PS_RDY arrive; ready() turns true at 5 V
 *   request()   builds the RDO for what the sketch wants, sends it, and
 *               waits (bounded) for the same Accept/PS_RDY dance
 *   PPS         maintain() re-sends the standing Request every few seconds,
 *               because a source drops a PPS contract that goes quiet
 *
 * Everything the interrupt handler and the sketch share is in the file-
 * static block below rather than in the class: the USBPD block is singular,
 * and a free-function ISR reaching into one static struct is the same
 * pattern the vector table already uses for Serial and Wire.
 */
#include "USBPD.h"

#include "Arduino.h"
#include "usbpd_hw.h"

#ifdef CH32_USBPD_BASE

namespace {

/* Longest frame either way: 2 header + 7 * 4 objects + 4 CRC, padded. */
constexpr uint8_t FRAME_MAX = 34;

enum : uint8_t {
    ST_DETACHED = 0,
    ST_WAIT_CAPS,     /* attached; listening for Source_Capabilities  */
    ST_WAIT_ACCEPT,   /* our Request is out                           */
    ST_WAIT_PS_RDY,   /* accepted; waiting for the supply to settle   */
    ST_READY,         /* explicit contract in place                   */
};

struct {
    volatile uint8_t state;
    volatile bool result_ok;      /* how the last Request ended            */
    pd_caps_t caps;               /* written in the handler, read by all   */
    volatile uint8_t rev;         /* spec revision mirrored from the source */
    volatile uint8_t tx_id;       /* our next MessageID                    */
    volatile uint8_t pending_len;    /* staged tx_buf bytes; sent on TX_END */
    volatile uint16_t cand_mv, cand_ma;   /* what pending_rdo asks for     */
    volatile uint16_t contract_mv, contract_ma;
    volatile bool contract_pps;
    volatile uint8_t tx_stage;    /* 0 idle, 1 GoodCRC out, 2 payload out  */
    uint32_t last_detect_ms;
    uint32_t last_pps_ms;
} pd;

/* DMA targets. The engine reads and writes RAM directly, so these cannot
 * live on anyone's stack. */
__attribute__((aligned(4))) uint8_t rx_buf[FRAME_MAX];
__attribute__((aligned(4))) uint8_t tx_buf[FRAME_MAX];
__attribute__((aligned(4))) uint8_t crc_buf[4];   /* a GoodCRC is header-only */

void rx_mode()
{
    CH32_USBPD_CONFIG |= CH32_UPD_PD_ALL_CLR;
    CH32_USBPD_CONFIG &= (uint16_t)~CH32_UPD_PD_ALL_CLR;
    CH32_USBPD_DMA = (uint32_t)rx_buf;
    CH32_USBPD_CONTROL &= (uint8_t)~CH32_UPD_PD_TX_EN;
    CH32_USBPD_BMC_CLK_CNT = CH32_UPD_TMR_RX;
    CH32_USBPD_CONTROL |= CH32_UPD_BMC_START;
}

void phy_send(const uint8_t *buf, uint8_t len, uint8_t sop)
{
    /* LVE turns the selected CC pad into a driver for the duration of the
     * frame; the TX_END handler releases it. */
    if (CH32_USBPD_CONFIG & CH32_UPD_CC_SEL) {
        CH32_USBPD_PORT_CC2 |= CH32_UPD_CC_LVE;
    } else {
        CH32_USBPD_PORT_CC1 |= CH32_UPD_CC_LVE;
    }
    CH32_USBPD_BMC_CLK_CNT = CH32_UPD_TMR_TX;
    CH32_USBPD_DMA = (uint32_t)buf;
    CH32_USBPD_TX_SEL = sop;
    CH32_USBPD_BMC_TX_SZ = len;
    CH32_USBPD_CONTROL |= CH32_UPD_PD_TX_EN;
    CH32_USBPD_STATUS = 0;
    CH32_USBPD_CONTROL |= CH32_UPD_BMC_START;
}

/* Queue-of-one for the protocol messages: the bytes go into tx_buf, and
 * either out at once or right after the GoodCRC already in flight. */
void stage_tx(uint8_t len)
{
    if (pd.tx_stage == 0) {
        pd.tx_stage = 2;
        phy_send(tx_buf, len, CH32_UPD_SOP0);
    } else {
        pd.pending_len = len;
    }
}

void send_request(uint32_t rdo)
{
    const uint16_t header = pd_header(PD_DATA_REQUEST, 1, pd.tx_id, pd.rev);
    tx_buf[0] = (uint8_t)header;
    tx_buf[1] = (uint8_t)(header >> 8);
    /* The flag both reference sinks set: no USB suspend behaviour here.
     * USB-comm-capable is left off because this sink genuinely is not. */
    const uint32_t word = rdo | PD_RDO_NO_USB_SUSPEND;
    tx_buf[2] = (uint8_t)word;
    tx_buf[3] = (uint8_t)(word >> 8);
    tx_buf[4] = (uint8_t)(word >> 16);
    tx_buf[5] = (uint8_t)(word >> 24);
    stage_tx(6);
}

void send_control(uint8_t type)
{
    const uint16_t header = pd_header(type, 0, pd.tx_id, pd.rev);
    tx_buf[0] = (uint8_t)header;
    tx_buf[1] = (uint8_t)(header >> 8);
    stage_tx(2);
}

/* Masks only the USBPD vector: what the handler and the sketch share is the
 * pd struct above, and SysTick has no business stopping for it. */
struct IrqLock {
    IrqLock() { ch32_irq_disable(CH32_USBPD_IRQ); }
    ~IrqLock() { ch32_irq_enable(CH32_USBPD_IRQ); }
};

void goodcrc(uint8_t their_id)
{
    const uint16_t header = pd_header(PD_CTRL_GOODCRC, 0, their_id, pd.rev);
    crc_buf[0] = (uint8_t)header;
    crc_buf[1] = (uint8_t)(header >> 8);
    pd.tx_stage = 1;
    phy_send(crc_buf, 2, CH32_UPD_SOP0);
}

void attach_reset()
{
    pd.state = ST_WAIT_CAPS;
    pd.caps.count = 0;
    pd.tx_id = 0;
    pd.tx_stage = 0;
    pd.pending_len = 0;
    /* vSafe5V is what a source provides before any contract exists. */
    pd.contract_mv = 5000;
    pd.contract_ma = 0;
    pd.contract_pps = false;
}

}  // namespace

namespace arduino {

bool CH32UsbPd::begin()
{
    ch32_clock_enable_at(CH32_USBPD_CLKEN_ADDR, CH32_USBPD_CLKEN_MASK);
    ch32_clock_enable(AFIO);
    CH32_USBPD_AFIO_CTLR |= CH32_USBPD_IN_HVT | CH32_USBPD_PHY_V33;

    CH32_USBPD_CONFIG = CH32_UPD_PD_DMA_EN;
    CH32_USBPD_STATUS = CH32_UPD_IF_ALL;              /* write-1-to-clear */

    /* A sink presents Rd on both CC lines and watches for a source's Rp. */
    CH32_USBPD_PORT_CC1 = CH32_UPD_CC_CMP_66 | CH32_UPD_CC_PD;
    CH32_USBPD_PORT_CC2 = CH32_UPD_CC_CMP_66 | CH32_UPD_CC_PD;

    pd.state = ST_DETACHED;
    pd.rev = PD_REV_3_0;
    pd.contract_mv = 0;
    pd.contract_ma = 0;
    pd.contract_pps = false;
    pd.caps.count = 0;
    pd.tx_stage = 0;
    pd.pending_len = 0;

    CH32_USBPD_CONFIG |= CH32_UPD_IE_RX_ACT | CH32_UPD_IE_RX_RESET |
                         CH32_UPD_IE_TX_END;
    ch32_irq_enable(CH32_USBPD_IRQ);
    rx_mode();
    return true;
}

void CH32UsbPd::end()
{
    ch32_irq_disable(CH32_USBPD_IRQ);
    CH32_USBPD_CONFIG = 0;
    CH32_USBPD_PORT_CC1 = 0;
    CH32_USBPD_PORT_CC2 = 0;
    ch32_clock_disable_at(CH32_USBPD_CLKEN_ADDR, CH32_USBPD_CLKEN_MASK);
    pd.state = ST_DETACHED;
    pd.caps.count = 0;
    pd.contract_mv = 0;
    pd.contract_ma = 0;
}

bool CH32UsbPd::connected() { return pd.state != ST_DETACHED; }
bool CH32UsbPd::ready()     { return pd.state == ST_READY; }

uint8_t CH32UsbPd::profileCount() const { return pd.caps.count; }

PDProfile CH32UsbPd::profile(uint8_t index) const
{
    if (index >= pd.caps.count) {
        PDProfile none = {PD_SUPPLY_UNKNOWN, 0, 0, 0, 0, 0};
        return none;
    }
    return pd.caps.pdo[index];
}

uint16_t CH32UsbPd::voltage() const { return pd.contract_mv; }
uint16_t CH32UsbPd::current() const { return pd.contract_ma; }

bool CH32UsbPd::request(uint16_t millivolts, uint16_t milliamps)
{
    return requestProfile((uint8_t)pd_pick(&pd.caps, millivolts, milliamps),
                          millivolts, milliamps);
}

bool CH32UsbPd::requestProfile(uint8_t index, uint16_t millivolts,
                               uint16_t milliamps)
{
    const int idx = index >= pd.caps.count ? -1 : index;
    /* A fixed profile's "voltage" argument is its own level; filling it in
     * here is what lets requestProfile(i) work without repeating it. */
    uint16_t mv = millivolts;
    if (idx >= 0 && pd.caps.pdo[idx].kind == PD_SUPPLY_FIXED) {
        mv = pd.caps.pdo[idx].max_mv;
    }
    const uint32_t rdo = pd_request_for(&pd.caps, idx, mv, milliamps);
    if (rdo == 0u || pd.state != ST_READY) {
        /* Nothing requestable, or a negotiation is already in flight - the
         * driver's own 5 V request right after attach included. ready() is
         * the green light the example waits for. */
        return false;
    }

    const uint16_t ma = milliamps ? milliamps : pd.caps.pdo[idx].max_ma;
    {
        IrqLock lock;
        pd.cand_mv = mv;
        pd.cand_ma = ma;
        pd.result_ok = false;
        pd.state = ST_WAIT_ACCEPT;
        send_request(rdo);
    }

    /* Accept plus PS_RDY has a spec budget well under half a second; twice
     * that, and the answer is "this source said no by silence". */
    const uint32_t t0 = millis();
    while (millis() - t0 < 800u) {
        if (pd.state == ST_READY) {
            if (pd.result_ok) {
                pd.last_pps_ms = millis();
            }
            return pd.result_ok;
        }
    }
    return false;
}

void CH32UsbPd::maintain()
{
    const uint32_t now = millis();

    if (pd.state == ST_DETACHED) {
        if (now - pd.last_detect_ms < 10u) {
            return;
        }
        pd.last_detect_ms = now;
        /* Drop each comparator to 0.22 V and look for a source's pull-up.
         * 2 us is the settle time the reference implementations use. */
        uint8_t found = 0;
        CH32_USBPD_PORT_CC1 &= (uint16_t)~(CH32_UPD_CC_CMP_MASK | CH32_UPD_PA_CC_AI);
        CH32_USBPD_PORT_CC1 |= CH32_UPD_CC_CMP_22;
        delayMicroseconds(2);
        if (CH32_USBPD_PORT_CC1 & CH32_UPD_PA_CC_AI) {
            found = 1;
        }
        CH32_USBPD_PORT_CC2 &= (uint16_t)~(CH32_UPD_CC_CMP_MASK | CH32_UPD_PA_CC_AI);
        CH32_USBPD_PORT_CC2 |= CH32_UPD_CC_CMP_22;
        delayMicroseconds(2);
        if (!found && (CH32_USBPD_PORT_CC2 & CH32_UPD_PA_CC_AI)) {
            found = 2;
        }
        /* Back to the idle threshold either way. */
        CH32_USBPD_PORT_CC1 = CH32_UPD_CC_CMP_66 | CH32_UPD_CC_PD;
        CH32_USBPD_PORT_CC2 = CH32_UPD_CC_CMP_66 | CH32_UPD_CC_PD;
        if (!found) {
            return;
        }
        if (found == 2) {
            CH32_USBPD_CONFIG |= CH32_UPD_CC_SEL;
        } else {
            CH32_USBPD_CONFIG &= (uint16_t)~CH32_UPD_CC_SEL;
        }
        attach_reset();
        rx_mode();
        return;
    }

    /* A PPS contract is dropped by the source unless the sink keeps asking;
     * half of the 10 s ceiling leaves room for a retry to still make it. */
    if (pd.state == ST_READY && pd.contract_pps
            && now - pd.last_pps_ms > 5000u) {
        pd.last_pps_ms = now;
        const int idx = pd_pick(&pd.caps, pd.contract_mv, pd.contract_ma);
        const uint32_t rdo = pd_request_for(&pd.caps, idx, pd.contract_mv,
                                            pd.contract_ma);
        if (rdo != 0u) {
            IrqLock lock;
            pd.cand_mv = pd.contract_mv;
            pd.cand_ma = pd.contract_ma;
            pd.state = ST_WAIT_ACCEPT;
            send_request(rdo);
        }
    }
}

void CH32UsbPd::irq()
{
    const uint8_t status = CH32_USBPD_STATUS;

    if (status & CH32_UPD_IF_RX_RESET) {
        CH32_USBPD_STATUS = CH32_UPD_IF_RX_RESET;
        /* A hard reset takes the bus back to 5 V and the source re-sends
         * its capabilities; mirror that. */
        attach_reset();
        rx_mode();
        return;
    }

    if (status & CH32_UPD_IF_TX_END) {
        CH32_USBPD_STATUS = CH32_UPD_IF_TX_END;
        CH32_USBPD_PORT_CC1 &= (uint16_t)~CH32_UPD_CC_LVE;
        CH32_USBPD_PORT_CC2 &= (uint16_t)~CH32_UPD_CC_LVE;
        if (pd.tx_stage == 1 && pd.pending_len) {
            /* The GoodCRC is out; now the message staged behind it. */
            const uint8_t len = pd.pending_len;
            pd.pending_len = 0;
            pd.tx_stage = 2;
            phy_send(tx_buf, len, CH32_UPD_SOP0);
            return;
        }
        pd.tx_stage = 0;
        rx_mode();
        return;
    }

    if (!(status & CH32_UPD_IF_RX_ACT)) {
        return;
    }
    CH32_USBPD_STATUS = CH32_UPD_IF_RX_ACT;

    if ((status & CH32_UPD_BMC_AUX_MASK) != CH32_UPD_AUX_SOP0) {
        rx_mode();
        return;
    }
    const uint16_t count = CH32_USBPD_BMC_BYTE_CNT;
    if (count < 6u) {                     /* header + CRC is the floor */
        rx_mode();
        return;
    }
    const uint16_t header = (uint16_t)(rx_buf[0] | (rx_buf[1] << 8));
    const uint8_t type = pd_header_type(header);
    const uint8_t ndo = pd_header_count(header);

    if (ndo == 0 && type == PD_CTRL_GOODCRC) {
        /* Ours arrived; the next message gets a fresh id. */
        pd.tx_id = (uint8_t)((pd.tx_id + 1u) & 0x7u);
        rx_mode();
        return;
    }

    /* Everything else is acknowledged first - the source retransmits
     * anything that is not - and acted on after. */
    pd.rev = pd_header_rev(header) == PD_REV_2_0 ? PD_REV_2_0 : PD_REV_3_0;
    goodcrc(pd_header_id(header));

    if (ndo > 0 && type == PD_DATA_SOURCE_CAP) {
        uint32_t words[PD_PDO_MAX];
        uint8_t n = ndo > PD_PDO_MAX ? PD_PDO_MAX : ndo;
        if ((uint16_t)(2u + 4u * n) > count - 4u) {
            n = (uint8_t)((count - 6u) / 4u);
        }
        for (uint8_t i = 0; i < n; i++) {
            words[i] = (uint32_t)rx_buf[2 + 4 * i]
                       | ((uint32_t)rx_buf[3 + 4 * i] << 8)
                       | ((uint32_t)rx_buf[4 + 4 * i] << 16)
                       | ((uint32_t)rx_buf[5 + 4 * i] << 24);
        }
        pd_parse_source_caps(words, n, &pd.caps);
        /* The spec's clock is running: a Source_Capabilities that is not
         * answered with a Request gets a hard reset. Profile 0 is the 5 V
         * everyone must offer, so that is the standing answer until the
         * sketch asks for something else. */
        const uint32_t rdo = pd_request_for(&pd.caps, 0, pd.caps.pdo[0].max_mv, 0);
        if (rdo != 0u) {
            pd.cand_mv = pd.caps.pdo[0].max_mv;
            pd.cand_ma = pd.caps.pdo[0].max_ma;
            pd.state = ST_WAIT_ACCEPT;
            send_request(rdo);
        }
        return;
    }

    if (ndo == 0) {
        switch (type) {
        case PD_CTRL_ACCEPT:
            if (pd.state == ST_WAIT_ACCEPT) {
                pd.state = ST_WAIT_PS_RDY;
            }
            break;
        case PD_CTRL_REJECT:
            if (pd.state == ST_WAIT_ACCEPT) {
                /* The old contract stands; the request just failed. */
                pd.result_ok = false;
                pd.state = ST_READY;
            }
            break;
        case PD_CTRL_PS_RDY:
            if (pd.state == ST_WAIT_PS_RDY) {
                pd.contract_mv = pd.cand_mv;
                pd.contract_ma = pd.cand_ma;
                const int idx = pd_pick(&pd.caps, pd.cand_mv, 0);
                pd.contract_pps =
                    idx >= 0 && pd.caps.pdo[idx].kind == PD_SUPPLY_PPS;
                pd.result_ok = true;
                pd.state = ST_READY;
            }
            break;
        case PD_CTRL_SOFT_RESET:
            /* Protocol-level restart: message ids to zero, contract kept.
             * The source waits for an Accept and then re-sends its
             * capabilities; the Accept goes out behind the GoodCRC. */
            pd.tx_id = 0;
            pd.state = ST_WAIT_CAPS;
            send_control(PD_CTRL_ACCEPT);
            break;
        default:
            /* A plain sink may ignore the rest. */
            break;
        }
    }
    /* GoodCRC is in flight; TX_END puts the receiver back on. */
}

}  // namespace arduino

extern "C" __attribute__((interrupt)) void USBPD_IRQHandler(void)
{
    USBPD.irq();
}

#else  /* no USBPD block on this part, or not brought up yet - see usbpd_hw.h */

namespace arduino {

bool CH32UsbPd::begin()     { return false; }
void CH32UsbPd::end()       {}
bool CH32UsbPd::connected() { return false; }
bool CH32UsbPd::ready()     { return false; }
uint8_t CH32UsbPd::profileCount() const { return 0; }

PDProfile CH32UsbPd::profile(uint8_t) const
{
    PDProfile none = {PD_SUPPLY_UNKNOWN, 0, 0, 0, 0, 0};
    return none;
}

uint16_t CH32UsbPd::voltage() const { return 0; }
uint16_t CH32UsbPd::current() const { return 0; }
bool CH32UsbPd::request(uint16_t, uint16_t) { return false; }
bool CH32UsbPd::requestProfile(uint8_t, uint16_t, uint16_t) { return false; }
void CH32UsbPd::maintain() {}
void CH32UsbPd::irq() {}

}  // namespace arduino

#endif /* CH32_USBPD_BASE */

arduino::CH32UsbPd USBPD;
