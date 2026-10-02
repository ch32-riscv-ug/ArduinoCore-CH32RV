/* The USB PD sink driver. Frame decisions live in pd_frames.c where the
 * tests reach them; this file owns the USBPD block - CC detection, the BMC
 * transmitter/receiver, GoodCRC, retransmission, and the sink policy engine.
 * USBPD.h says what it does and what has been checked against a charger.
 *
 * Two halves share the file-static block below:
 *
 *   irq()    everything that has a microsecond deadline: GoodCRC goes out
 *            within tTransmit of a message, and the answer the message needs
 *            (a Request to Source_Capabilities, Accept to Soft_Reset,
 *            Sink_Capabilities, Not_Supported) is staged right behind it
 *   poll()   everything measured in milliseconds: attach and detach on CC,
 *            tTypeCSinkWaitCap, the response timers, retransmitting a message
 *            nobody GoodCRC'd, the PPS keepalive. Every public call runs it,
 *            so a sketch waiting on ready() alone still reaches a contract.
 *
 * The USBPD block is singular, and a free-function ISR reaching into one
 * static struct is the pattern the vector table already uses for Serial and
 * Wire.
 */
#include "USBPD.h"

#include "Arduino.h"
#include "usbpd_hw.h"

#ifdef CH32RV_USBPD_BASE

namespace {

/* Longest frame either way: 2 header + 7 * 4 objects + 4 CRC, padded. */
constexpr uint8_t FRAME_MAX = 34;

/* The spec's timers (USB PD R3.1 §6.6), at the end of their ranges that
 * gives the other side the most room. */
constexpr uint32_t T_DETECT_MS          = 10;     /* attach polling                     */
constexpr uint32_t T_SINK_WAIT_CAP_MS   = 620;    /* tTypeCSinkWaitCap / SinkWaitCapTimer */
constexpr uint32_t T_SENDER_RESPONSE_MS = 30;     /* tSenderResponse                    */
constexpr uint32_t T_PS_TRANSITION_MS   = 550;    /* tPSTransition                      */
constexpr uint32_t T_HARD_RESET_MS      = 3000;   /* source off, back on, caps again    */
constexpr uint32_t T_SINK_REQUEST_MS    = 100;    /* tSinkRequest: after a Wait         */
constexpr uint32_t T_PPS_MS             = 5000;   /* half of SinkPPSPeriodicTimer (10 s) */
constexpr uint32_t T_RECEIVE_US         = 1100;   /* tReceive: GoodCRC due after a send */
constexpr uint32_t T_DETACH_POLL_MS     = 50;
constexpr uint8_t  N_RETRY              = 2;      /* nRetryCount                        */
constexpr uint8_t  N_HARD_RESET         = 2;      /* nHardResetCount                    */
constexpr uint8_t  N_DETACH             = 3;      /* CC low this many polls in a row    */

/* The 5 V the sink is happy with, for Sink_Capabilities. 3 A is "anything
 * the cable allows": the source caps it to its own Rp anyway. */
constexpr uint16_t SINK_MV = 5000;
constexpr uint16_t SINK_MA = 3000;

enum : uint8_t {
    ST_DETACHED = 0,
    ST_WAIT_CAPS,       /* attached; listening for Source_Capabilities          */
    ST_WAIT_SOFT_ACK,   /* our Soft_Reset is out; waiting for its Accept        */
    ST_WAIT_ACCEPT,     /* our Request is out                                   */
    ST_WAIT_PS_RDY,     /* accepted; waiting for the supply to settle           */
    ST_READY,           /* explicit contract in place                           */
    ST_NO_PD,           /* attached, but the source never spoke PD (Type-C only) */
};

enum : uint8_t { RES_NONE = 0, RES_OK, RES_REJECT, RES_WAIT, RES_FAIL };

/* What the last message we sent was, for what to do when it is lost. */
enum : uint8_t { TX_NONE = 0, TX_OTHER, TX_REQUEST, TX_SOFT_RESET };

struct {
    volatile uint8_t state;
    volatile uint32_t phase_ms;        /* when the state was entered            */
    bool on;                           /* between begin() and end()             */
    pd_caps_t caps;                    /* written in the handler, read by all   */
    volatile uint8_t rev;              /* spec revision mirrored from the source */

    /* protocol layer */
    volatile uint8_t tx_id;            /* our next MessageID                    */
    volatile int8_t rx_id;             /* last MessageID received, -1 none      */
    volatile uint8_t tx_stage;         /* 0 idle, 1 GoodCRC out, 2 message out,
                                          3 hard reset out                      */
    volatile uint8_t staged_len;       /* tx_buf bytes waiting behind a GoodCRC */
    volatile uint8_t tx_len;           /* tx_buf bytes of the message in hand   */
    volatile uint8_t tx_kind;          /* TX_*: what the message in hand is     */
    volatile bool await_crc;           /* the message in hand wants a GoodCRC   */
    volatile uint32_t tx_done_us;      /* when it finished going out            */
    volatile uint8_t retries;

    /* policy */
    volatile uint8_t result;           /* RES_*: how the last Request ended     */
    volatile bool user_request;        /* the Request in flight is the sketch's */
    volatile int8_t cand_idx;          /* what the Request in flight asks for   */
    volatile uint16_t cand_mv, cand_ma;
    volatile int8_t contract_idx;
    volatile uint16_t contract_mv, contract_ma;
    volatile bool contract_pps;
    int8_t want_idx;                   /* the sketch's last good choice, -1 none */
    uint16_t want_mv, want_ma;
    uint32_t want_raw;                 /* the PDO it was made against           */
    volatile uint8_t hard_resets;
    volatile bool soft_tried;
    volatile uint32_t wait_ms;         /* how long ST_WAIT_CAPS may last        */

    uint32_t last_detect_ms;
    uint32_t last_pps_ms;
    volatile uint32_t last_rx_ms;
    uint8_t detach_lows;
} pd;

/* DMA targets. The engine reads and writes RAM directly, so these cannot
 * live on anyone's stack. */
__attribute__((aligned(4))) uint8_t rx_buf[FRAME_MAX];
__attribute__((aligned(4))) uint8_t tx_buf[FRAME_MAX];
__attribute__((aligned(4))) uint8_t crc_buf[4];   /* a GoodCRC is header-only */

/* Masks only the USBPD vector: what the handler and the sketch share is the
 * pd struct above, and SysTick has no business stopping for it. */
struct IrqLock {
    IrqLock() { ch32rv_irq_disable(CH32RV_USBPD_IRQ); }
    ~IrqLock() { ch32rv_irq_enable(CH32RV_USBPD_IRQ); }
};

void enter(uint8_t state)
{
    pd.state = state;
    pd.phase_ms = millis();
}

void rx_mode()
{
    CH32RV_USBPD_CONFIG |= CH32RV_UPD_PD_ALL_CLR;
    CH32RV_USBPD_CONFIG &= (uint16_t)~CH32RV_UPD_PD_ALL_CLR;
    CH32RV_USBPD_DMA = (uint32_t)rx_buf;
    CH32RV_USBPD_CONTROL &= (uint8_t)~CH32RV_UPD_PD_TX_EN;
    CH32RV_USBPD_BMC_CLK_CNT = CH32RV_UPD_TMR_RX;
    CH32RV_USBPD_CONTROL |= CH32RV_UPD_BMC_START;
}

void phy_send(const uint8_t *buf, uint8_t len, uint8_t sop)
{
    /* LVE turns the selected CC pad into a driver for the duration of the
     * frame; the TX_END handler releases it. */
    if (CH32RV_USBPD_CONFIG & CH32RV_UPD_CC_SEL) {
        CH32RV_USBPD_PORT_CC2 |= CH32RV_UPD_CC_LVE;
    } else {
        CH32RV_USBPD_PORT_CC1 |= CH32RV_UPD_CC_LVE;
    }
    CH32RV_USBPD_BMC_CLK_CNT = CH32RV_UPD_TMR_TX;
    CH32RV_USBPD_DMA = (uint32_t)buf;
    CH32RV_USBPD_TX_SEL = sop;
    CH32RV_USBPD_BMC_TX_SZ = len;
    CH32RV_USBPD_CONTROL |= CH32RV_UPD_PD_TX_EN;
    CH32RV_USBPD_STATUS = 0;
    CH32RV_USBPD_CONTROL |= CH32RV_UPD_BMC_START;
}

/* The message in tx_buf goes out now, or right after the GoodCRC already in
 * flight. Called from the handler, or from poll() under IrqLock. */
void send_staged(uint8_t len, uint8_t kind)
{
    pd.tx_len = len;
    pd.tx_kind = kind;
    pd.await_crc = true;
    pd.retries = 0;
    if (pd.tx_stage == 0) {
        pd.tx_stage = 2;
        phy_send(tx_buf, len, CH32RV_UPD_SOP0);
    } else {
        pd.staged_len = len;
    }
}

void put_header(uint8_t type, uint8_t count)
{
    const uint16_t header = pd_header(type, count, pd.tx_id, pd.rev);
    tx_buf[0] = (uint8_t)header;
    tx_buf[1] = (uint8_t)(header >> 8);
}

void put_word(uint8_t at, uint32_t word)
{
    tx_buf[at] = (uint8_t)word;
    tx_buf[at + 1] = (uint8_t)(word >> 8);
    tx_buf[at + 2] = (uint8_t)(word >> 16);
    tx_buf[at + 3] = (uint8_t)(word >> 24);
}

void send_control(uint8_t type, uint8_t kind = TX_OTHER)
{
    put_header(type, 0);
    send_staged(2, kind);
}

/* The Request for profile idx; false when that profile cannot be asked for. */
bool send_request(int8_t idx, uint16_t mv, uint16_t ma, bool user)
{
    const uint32_t rdo = pd_request_for(&pd.caps, idx, mv, ma);
    if (rdo == 0u) {
        return false;
    }
    const pd_pdo_t &p = pd.caps.pdo[idx];
    /* What the RDO really carries: PPS asks in 20 mV / 50 mA steps, a fixed
     * profile in 10 mA steps - voltage() and current() report that. */
    const bool pps = p.kind == PD_SUPPLY_PPS;
    if (ma == 0u || ma > p.max_ma) {
        ma = p.max_ma;
    }
    pd.cand_idx = idx;
    pd.cand_mv = pps ? (uint16_t)(mv / 20u * 20u) : p.max_mv;
    pd.cand_ma = (uint16_t)(ma / (pps ? 50u : 10u) * (pps ? 50u : 10u));
    pd.user_request = user;
    pd.result = RES_NONE;
    put_header(PD_DATA_REQUEST, 1);
    /* The flag both reference sinks set: no USB suspend behaviour here.
     * USB-comm-capable is left off because this sink genuinely is not. */
    put_word(2, rdo | PD_RDO_NO_USB_SUSPEND);
    enter(ST_WAIT_ACCEPT);
    send_staged(6, TX_REQUEST);
    return true;
}

void send_sink_caps()
{
    put_header(PD_DATA_SINK_CAP, 1);
    put_word(2, pd_sink_pdo_fixed(SINK_MV, SINK_MA, 0));
    send_staged(6, TX_OTHER);
}

/* Unsupported message: PD 3.0 says Not_Supported, PD 2.0 says Reject. */
void send_unsupported()
{
    send_control(pd.rev >= PD_REV_3_0 ? PD_CTRL_NOT_SUPPORTED : PD_CTRL_REJECT);
}

void goodcrc(uint8_t their_id)
{
    const uint16_t header = pd_header(PD_CTRL_GOODCRC, 0, their_id, pd.rev);
    crc_buf[0] = (uint8_t)header;
    crc_buf[1] = (uint8_t)(header >> 8);
    pd.tx_stage = 1;
    phy_send(crc_buf, 2, CH32RV_UPD_SOP0);
}

/* Both ends forget message ids: on attach, Soft_Reset and Hard Reset. */
void protocol_reset()
{
    pd.tx_id = 0;
    pd.rx_id = -1;
    pd.await_crc = false;
    pd.tx_kind = TX_NONE;
    pd.staged_len = 0;
}

void no_contract()
{
    /* vSafe5V is what a source provides before any contract exists. */
    pd.contract_idx = -1;
    pd.contract_mv = 5000;
    pd.contract_ma = 0;
    pd.contract_pps = false;
}

void wait_caps(uint32_t how_long_ms)
{
    pd.wait_ms = how_long_ms;
    enter(ST_WAIT_CAPS);
}

void soft_reset()
{
    protocol_reset();
    pd.soft_tried = true;
    send_control(PD_CTRL_SOFT_RESET, TX_SOFT_RESET);
    enter(ST_WAIT_SOFT_ACK);
}

void hard_reset()
{
    protocol_reset();
    pd.hard_resets++;
    pd.caps.count = 0;
    no_contract();
    pd.tx_stage = 3;
    phy_send(tx_buf, 0, CH32RV_UPD_HARD_RESET);
    /* The source answers by switching VBUS off and back to 5 V, then sends
     * its capabilities again. A board powered only from VBUS restarts. */
    wait_caps(T_HARD_RESET_MS);
}

void attach(uint8_t line)
{
    if (line == 2) {
        CH32RV_USBPD_CONFIG |= CH32RV_UPD_CC_SEL;
    } else {
        CH32RV_USBPD_CONFIG &= (uint16_t)~CH32RV_UPD_CC_SEL;
    }
    protocol_reset();
    pd.tx_stage = 0;
    pd.caps.count = 0;
    pd.hard_resets = 0;
    pd.soft_tried = false;
    pd.result = RES_NONE;
    pd.detach_lows = 0;
    no_contract();
    wait_caps(T_SINK_WAIT_CAP_MS);
    rx_mode();
}

void detach()
{
    protocol_reset();
    pd.tx_stage = 0;
    pd.caps.count = 0;
    pd.result = RES_FAIL;
    no_contract();
    pd.contract_mv = 0;
    pd.want_idx = -1;           /* whatever attaches next is a new charger */
    enter(ST_DETACHED);
    CH32RV_USBPD_PORT_CC1 = CH32RV_UPD_CC_CMP_66 | CH32RV_UPD_CC_PD;
    CH32RV_USBPD_PORT_CC2 = CH32RV_UPD_CC_CMP_66 | CH32RV_UPD_CC_PD;
    rx_mode();
}

/* One CC line against vRd-Connect (0.22 V): a source's Rp is there. The
 * comparator goes back to the idle threshold either way. */
bool cc_sees_source(volatile uint16_t &port)
{
    port &= (uint16_t)~(CH32RV_UPD_CC_CMP_MASK | CH32RV_UPD_PA_CC_AI);
    port |= CH32RV_UPD_CC_CMP_22;
    delayMicroseconds(2);        /* the settle time the reference sinks use */
    const bool hit = port & CH32RV_UPD_PA_CC_AI;
    port = CH32RV_UPD_CC_CMP_66 | CH32RV_UPD_CC_PD;
    return hit;
}

/* Source_Capabilities arrived (handler context): answer with a Request -
 * the sketch's last good choice if this charger still offers it, else the
 * 5 V profile 0. */
void on_source_caps(uint8_t n)
{
    uint32_t words[PD_PDO_MAX];
    for (uint8_t i = 0; i < n; i++) {
        words[i] = (uint32_t)rx_buf[2 + 4 * i]
                   | ((uint32_t)rx_buf[3 + 4 * i] << 8)
                   | ((uint32_t)rx_buf[4 + 4 * i] << 16)
                   | ((uint32_t)rx_buf[5 + 4 * i] << 24);
    }
    pd_parse_source_caps(words, n, &pd.caps);
    pd.soft_tried = false;
    /* The sketch's choice is asked for again only against the very same
     * PDO: a profile number means nothing on another table (a 3.3-11 V PPS
     * entry can be a 15 V fixed one there). Anything else gets 5 V. */
    if (pd.want_idx >= 0 && pd.want_idx < pd.caps.count &&
        pd.caps.pdo[pd.want_idx].raw == pd.want_raw &&
        send_request(pd.want_idx, pd.want_mv, pd.want_ma, false)) {
        return;
    }
    pd.want_idx = -1;
    if (!send_request(0, pd.caps.count ? pd.caps.pdo[0].max_mv : 5000, 0, false)) {
        /* A table without a usable 5 V first entry is malformed. */
        send_control(PD_CTRL_SOFT_RESET, TX_SOFT_RESET);
    }
}

void on_control(uint8_t type)
{
    switch (type) {
    case PD_CTRL_ACCEPT:
        if (pd.state == ST_WAIT_ACCEPT) {
            enter(ST_WAIT_PS_RDY);
        } else if (pd.state == ST_WAIT_SOFT_ACK) {
            /* Soft_Reset done: the source sends its capabilities next. */
            wait_caps(T_SINK_WAIT_CAP_MS);
        }
        break;
    case PD_CTRL_REJECT:
    case PD_CTRL_WAIT:
        if (pd.state == ST_WAIT_ACCEPT) {
            /* The old contract stands; the request just did not happen. */
            pd.result = type == PD_CTRL_WAIT ? RES_WAIT : RES_REJECT;
            if (pd.contract_idx >= 0) {
                enter(ST_READY);
            } else {
                wait_caps(T_SINK_WAIT_CAP_MS);
            }
        }
        break;
    case PD_CTRL_PS_RDY:
        if (pd.state == ST_WAIT_PS_RDY) {
            pd.contract_idx = pd.cand_idx;
            pd.contract_mv = pd.cand_mv;
            pd.contract_ma = pd.cand_ma;
            pd.contract_pps = pd.caps.pdo[pd.cand_idx].kind == PD_SUPPLY_PPS;
            pd.result = RES_OK;
            pd.hard_resets = 0;
            pd.last_pps_ms = millis();
            enter(ST_READY);
        }
        break;
    case PD_CTRL_SOFT_RESET:
        /* Protocol-level restart: ids to zero, contract kept, Accept back;
         * the source re-sends its capabilities. */
        protocol_reset();
        send_control(PD_CTRL_ACCEPT);
        wait_caps(T_SINK_WAIT_CAP_MS);
        break;
    case PD_CTRL_GET_SINK_CAP:
        send_sink_caps();
        break;
    case PD_CTRL_PING:
    case PD_CTRL_GOTOMIN:          /* no GiveBack in our Requests: nothing to do */
        break;
    default:
        send_unsupported();
        break;
    }
}

void on_message(uint16_t count)
{
    const uint16_t header = (uint16_t)(rx_buf[0] | (rx_buf[1] << 8));
    const uint8_t type = pd_header_type(header);
    const uint8_t ndo = pd_header_count(header);
    const uint8_t id = pd_header_id(header);

    if (ndo == 0 && type == PD_CTRL_GOODCRC) {
        /* Ours arrived; the next message gets a fresh id. */
        if (pd.await_crc && id == pd.tx_id) {
            pd.await_crc = false;
            pd.tx_id = (uint8_t)((pd.tx_id + 1u) & 0x7u);
        }
        rx_mode();
        return;
    }

    /* Everything else is acknowledged first - the source retransmits
     * anything that is not - and acted on after. */
    pd.rev = pd_header_rev(header) == PD_REV_2_0 ? PD_REV_2_0 : PD_REV_3_0;
    goodcrc(id);
    const bool soft = ndo == 0 && type == PD_CTRL_SOFT_RESET;
    if (!soft && (int8_t)id == pd.rx_id) {
        return;                  /* a repeat whose GoodCRC was lost: done already */
    }
    pd.rx_id = (int8_t)id;

    if (pd_header_extended(header)) {
        send_unsupported();      /* no extended message is implemented */
        return;
    }
    if (ndo == 0) {
        on_control(type);
        return;
    }
    switch (type) {
    case PD_DATA_SOURCE_CAP: {
        uint8_t n = ndo > PD_PDO_MAX ? PD_PDO_MAX : ndo;
        if ((uint16_t)(2u + 4u * n) > count - 4u) {
            n = (uint8_t)((count - 6u) / 4u);
        }
        on_source_caps(n);
        break;
    }
    case PD_DATA_BIST:           /* compliance-test modes: not implemented, ignored */
    case PD_DATA_ALERT:          /* nothing the sink reports on */
        break;
    case PD_DATA_VENDOR_DEFINED:
        if (pd.rev >= PD_REV_3_0) {
            send_unsupported();  /* PD 2.0 ignores a VDM it does not speak */
        }
        break;
    default:
        send_unsupported();
        break;
    }
}

void poll()
{
    if (!pd.on) {
        return;
    }
    const uint32_t now = millis();

    if (pd.state == ST_DETACHED) {
        if (now - pd.last_detect_ms < T_DETECT_MS) {
            return;
        }
        pd.last_detect_ms = now;
        uint8_t line = 0;
        if (cc_sees_source(CH32RV_USBPD_PORT_CC1)) {
            line = 1;
        } else if (cc_sees_source(CH32RV_USBPD_PORT_CC2)) {
            line = 2;
        }
        if (line) {
            IrqLock lock;
            attach(line);
        }
        return;
    }

    IrqLock lock;

    /* A message nobody GoodCRC'd goes out again, twice; then it is a protocol
     * error - a lost Soft_Reset escalates to Hard Reset, anything else to
     * Soft_Reset. */
    if (pd.await_crc && pd.tx_stage == 0 && micros() - pd.tx_done_us > T_RECEIVE_US) {
        if (pd.retries < N_RETRY) {
            pd.retries++;
            pd.tx_stage = 2;
            phy_send(tx_buf, pd.tx_len, CH32RV_UPD_SOP0);
            return;
        }
        pd.await_crc = false;
        if (pd.tx_kind == TX_SOFT_RESET) {
            if (pd.hard_resets < N_HARD_RESET) {
                hard_reset();
            } else {
                enter(ST_NO_PD);
            }
        } else {
            if (pd.tx_kind == TX_REQUEST) {
                pd.result = RES_FAIL;
            }
            soft_reset();
        }
        return;
    }

    const uint32_t in_state = now - pd.phase_ms;
    switch (pd.state) {
    case ST_WAIT_CAPS:
        /* Nothing came: the source finished its announcements before we
         * listened (we reset, or were re-flashed). Soft_Reset makes it send
         * them again without touching VBUS; Hard Reset if that fails too. */
        if (in_state > pd.wait_ms && pd.tx_stage == 0) {
            if (!pd.soft_tried) {
                soft_reset();
            } else if (pd.hard_resets < N_HARD_RESET) {
                hard_reset();
            } else {
                enter(ST_NO_PD);
            }
        }
        break;
    case ST_WAIT_SOFT_ACK:
        if (in_state > T_SENDER_RESPONSE_MS + 5u && !pd.await_crc) {
            if (pd.hard_resets < N_HARD_RESET) {
                hard_reset();
            } else {
                enter(ST_NO_PD);
            }
        }
        break;
    case ST_WAIT_ACCEPT:
        /* tSenderResponse runs from our Request's GoodCRC. */
        if (!pd.await_crc && in_state > T_SENDER_RESPONSE_MS + 5u) {
            pd.result = RES_FAIL;
            hard_reset();
        }
        break;
    case ST_WAIT_PS_RDY:
        if (in_state > T_PS_TRANSITION_MS) {
            pd.result = RES_FAIL;
            hard_reset();
        }
        break;
    case ST_READY:
        /* The source drops a PPS contract that goes quiet; re-request it
         * with half of the 10 s ceiling to spare. */
        if (pd.contract_pps && now - pd.last_pps_ms > T_PPS_MS && pd.tx_stage == 0) {
            pd.last_pps_ms = now;
            send_request(pd.contract_idx, pd.contract_mv, pd.contract_ma, false);
        }
        break;
    default:
        break;
    }

    /* Detach: CC under vRd-Connect for a few polls in a row. Only checked
     * while the line is quiet, so a message is not sampled mid-flight. */
    if (pd.state != ST_DETACHED && pd.tx_stage == 0 && !pd.await_crc
            && now - pd.last_detect_ms >= T_DETACH_POLL_MS && now - pd.last_rx_ms > 5u) {
        pd.last_detect_ms = now;
        const bool cc2 = CH32RV_USBPD_CONFIG & CH32RV_UPD_CC_SEL;
        const bool there = cc_sees_source(cc2 ? CH32RV_USBPD_PORT_CC2 : CH32RV_USBPD_PORT_CC1);
        pd.detach_lows = there ? 0 : (uint8_t)(pd.detach_lows + 1u);
        if (pd.detach_lows >= N_DETACH) {
            detach();
        }
    }
}

/* Wait (up to timeout_ms) for a Request in flight to end. */
uint8_t wait_result(uint32_t timeout_ms)
{
    const uint32_t t0 = millis();
    while (millis() - t0 < timeout_ms) {
        poll();
        const uint8_t s = pd.state;
        if (s == ST_READY || s == ST_DETACHED || s == ST_NO_PD
                || (s == ST_WAIT_CAPS && pd.result != RES_NONE)) {
            return pd.result == RES_NONE ? RES_FAIL : pd.result;
        }
    }
    return RES_FAIL;
}

}  // namespace

namespace arduino {

bool CH32RVUsbPd::begin()
{
    ch32rv_clock_enable_at(CH32RV_USBPD_CLKEN_ADDR, CH32RV_USBPD_CLKEN_MASK);
    ch32rv_clock_enable(AFIO);
    CH32RV_USBPD_AFIO_CTLR |= CH32RV_USBPD_IN_HVT | CH32RV_USBPD_PHY_V33;

    CH32RV_USBPD_CONFIG = CH32RV_UPD_PD_DMA_EN;
    CH32RV_USBPD_STATUS = CH32RV_UPD_IF_ALL;              /* write-1-to-clear */

    /* A sink presents Rd on both CC lines and watches for a source's Rp. */
    CH32RV_USBPD_PORT_CC1 = CH32RV_UPD_CC_CMP_66 | CH32RV_UPD_CC_PD;
    CH32RV_USBPD_PORT_CC2 = CH32RV_UPD_CC_CMP_66 | CH32RV_UPD_CC_PD;

    pd.rev = PD_REV_3_0;
    pd.want_idx = -1;
    protocol_reset();
    pd.tx_stage = 0;
    pd.caps.count = 0;
    pd.result = RES_NONE;
    no_contract();
    pd.contract_mv = 0;
    enter(ST_DETACHED);
    pd.on = true;

    CH32RV_USBPD_CONFIG |= CH32RV_UPD_IE_RX_ACT | CH32RV_UPD_IE_RX_RESET |
                         CH32RV_UPD_IE_TX_END;
    ch32rv_irq_enable(CH32RV_USBPD_IRQ);
    rx_mode();
    pd.last_detect_ms = millis() - T_DETECT_MS;
    poll();                     /* look at CC right away */
    return true;
}

void CH32RVUsbPd::end()
{
    pd.on = false;
    ch32rv_irq_disable(CH32RV_USBPD_IRQ);
    CH32RV_USBPD_CONFIG = 0;
    CH32RV_USBPD_PORT_CC1 = 0;
    CH32RV_USBPD_PORT_CC2 = 0;
    ch32rv_clock_disable_at(CH32RV_USBPD_CLKEN_ADDR, CH32RV_USBPD_CLKEN_MASK);
    pd.state = ST_DETACHED;
    pd.caps.count = 0;
    pd.contract_idx = -1;
    pd.contract_mv = 0;
    pd.contract_ma = 0;
    pd.contract_pps = false;
}

bool CH32RVUsbPd::connected()
{
    poll();
    return pd.state != ST_DETACHED;
}

bool CH32RVUsbPd::ready()
{
    poll();
    return pd.state == ST_READY;
}

uint8_t CH32RVUsbPd::profileCount() const { return pd.caps.count; }

PDProfile CH32RVUsbPd::profile(uint8_t index) const
{
    if (index >= pd.caps.count) {
        PDProfile none = {PD_SUPPLY_UNKNOWN, 0, 0, 0, 0, 0};
        return none;
    }
    return pd.caps.pdo[index];
}

uint16_t CH32RVUsbPd::voltage() const { return pd.state == ST_DETACHED ? 0 : pd.contract_mv; }
uint16_t CH32RVUsbPd::current() const { return pd.contract_ma; }
int8_t CH32RVUsbPd::contractProfile() const { return pd.on ? pd.contract_idx : -1; }
bool CH32RVUsbPd::pps() const { return pd.contract_pps; }

bool CH32RVUsbPd::request(uint16_t millivolts, uint16_t milliamps)
{
    /* Whatever is in flight (the driver's own 5 V Request after attach, a
     * PPS keepalive) ends first. */
    if (pd.state != ST_READY && pd.state != ST_DETACHED && pd.state != ST_NO_PD) {
        wait_result(T_SINK_WAIT_CAP_MS + T_PS_TRANSITION_MS);
    }
    const int idx = pd_pick(&pd.caps, millivolts, milliamps);
    if (idx < 0) {
        return false;
    }
    return requestProfile((uint8_t)idx, millivolts, milliamps);
}

bool CH32RVUsbPd::requestProfile(uint8_t index, uint16_t millivolts,
                               uint16_t milliamps)
{
    poll();
    if (pd.state != ST_READY) {
        wait_result(T_SINK_WAIT_CAP_MS + T_PS_TRANSITION_MS);
        if (pd.state != ST_READY) {
            return false;
        }
    }
    if (index >= pd.caps.count) {
        return false;
    }
    const pd_pdo_t p = pd.caps.pdo[index];
    const uint16_t mv = p.kind == PD_SUPPLY_FIXED ? p.max_mv : millivolts;

    /* A Wait asks the sink to try again after tSinkRequest; give it a few
     * goes inside about a second. */
    for (uint8_t attempt = 0; attempt < 5; attempt++) {
        {
            IrqLock lock;
            if (pd.state != ST_READY || !send_request((int8_t)index, mv, milliamps, true)) {
                return false;
            }
        }
        const uint8_t r = wait_result(T_SENDER_RESPONSE_MS + T_PS_TRANSITION_MS + 100u);
        if (r == RES_OK) {
            pd.want_idx = (int8_t)index;
            pd.want_mv = mv;
            pd.want_ma = milliamps;
            pd.want_raw = pd.caps.pdo[index].raw;
            return true;
        }
        if (r != RES_WAIT) {
            return false;
        }
        const uint32_t t0 = millis();
        while (millis() - t0 < T_SINK_REQUEST_MS) {
            poll();
        }
    }
    return false;
}

void CH32RVUsbPd::maintain()
{
    poll();
}

void CH32RVUsbPd::irq()
{
    const uint8_t status = CH32RV_USBPD_STATUS;

    if (status & CH32RV_UPD_IF_RX_RESET) {
        CH32RV_USBPD_STATUS = CH32RV_UPD_IF_RX_RESET;
        /* The source's Hard Reset: VBUS goes to 0 and back to 5 V, then
         * capabilities again. */
        protocol_reset();
        pd.tx_stage = 0;
        pd.caps.count = 0;
        if (pd.state == ST_WAIT_ACCEPT || pd.state == ST_WAIT_PS_RDY) {
            pd.result = RES_FAIL;
        }
        no_contract();
        wait_caps(T_HARD_RESET_MS);
        rx_mode();
        return;
    }

    if (status & CH32RV_UPD_IF_TX_END) {
        CH32RV_USBPD_STATUS = CH32RV_UPD_IF_TX_END;
        CH32RV_USBPD_PORT_CC1 &= (uint16_t)~CH32RV_UPD_CC_LVE;
        CH32RV_USBPD_PORT_CC2 &= (uint16_t)~CH32RV_UPD_CC_LVE;
        if (pd.tx_stage == 1 && pd.staged_len) {
            /* The GoodCRC is out; now the message staged behind it. */
            const uint8_t len = pd.staged_len;
            pd.staged_len = 0;
            pd.tx_stage = 2;
            phy_send(tx_buf, len, CH32RV_UPD_SOP0);
            return;
        }
        if (pd.tx_stage == 2) {
            pd.tx_done_us = micros();     /* tReceive counts from here */
        }
        pd.tx_stage = 0;
        rx_mode();
        return;
    }

    if (!(status & CH32RV_UPD_IF_RX_ACT)) {
        return;
    }
    CH32RV_USBPD_STATUS = CH32RV_UPD_IF_RX_ACT;
    pd.last_rx_ms = millis();

    if ((status & CH32RV_UPD_BMC_AUX_MASK) != CH32RV_UPD_AUX_SOP0) {
        rx_mode();
        return;
    }
    const uint16_t count = CH32RV_USBPD_BMC_BYTE_CNT;
    if (count < 6u) {                     /* header + CRC is the floor */
        rx_mode();
        return;
    }
    on_message(count);
    /* A GoodCRC is in flight; TX_END puts the receiver back on. */
}

}  // namespace arduino

extern "C" __attribute__((interrupt)) void USBPD_IRQHandler(void)
{
    USBPD.irq();
}

#else  /* no USBPD block on this part, or not brought up yet - see usbpd_hw.h */

namespace arduino {

bool CH32RVUsbPd::begin()     { return false; }
void CH32RVUsbPd::end()       {}
bool CH32RVUsbPd::connected() { return false; }
bool CH32RVUsbPd::ready()     { return false; }
uint8_t CH32RVUsbPd::profileCount() const { return 0; }

PDProfile CH32RVUsbPd::profile(uint8_t) const
{
    PDProfile none = {PD_SUPPLY_UNKNOWN, 0, 0, 0, 0, 0};
    return none;
}

uint16_t CH32RVUsbPd::voltage() const { return 0; }
uint16_t CH32RVUsbPd::current() const { return 0; }
int8_t CH32RVUsbPd::contractProfile() const { return -1; }
bool CH32RVUsbPd::pps() const { return false; }
bool CH32RVUsbPd::request(uint16_t, uint16_t) { return false; }
bool CH32RVUsbPd::requestProfile(uint8_t, uint16_t, uint16_t) { return false; }
void CH32RVUsbPd::maintain() {}
void CH32RVUsbPd::irq() {}

}  // namespace arduino

#endif /* CH32RV_USBPD_BASE */

arduino::CH32RVUsbPd USBPD;
