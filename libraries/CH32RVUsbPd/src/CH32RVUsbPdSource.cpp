/* The USB PD source driver. Same split as the sink (CH32RVUsbPd.cpp):
 *
 *   irq()    microsecond deadlines: GoodCRC within tTransmit of a message,
 *            and the answer staged behind it - Accept or Reject to a Request,
 *            Source_Capabilities to Get_Source_Cap, Accept to Soft_Reset,
 *            Not_Supported to the rest
 *   poll()   millisecond ones: Rd detection and its debounce, VBUS on and
 *            off, resending Source_Capabilities, tSenderResponse, the
 *            voltage change and PS_RDY, the PPS timeout, Hard Reset recovery,
 *            detach. Every public call runs it.
 *
 * The sketch's two callbacks are only ever called from poll(), never from
 * the interrupt: they switch real power and may take milliseconds.
 */
#include "CH32RVUsbPdSource.h"

#include "Arduino.h"
#include "usbpd_hw.h"

#ifdef CH32RV_USBPD_BASE

namespace {

constexpr uint8_t FRAME_MAX = 34;

/* USB PD R3.1 §6.6 and Type-C 2.x §4.11, at the end of each range that gives
 * the sink the most room. */
constexpr uint32_t T_DETECT_MS            = 10;     /* CC polling                          */
constexpr uint32_t T_CC_DEBOUNCE_MS       = 150;    /* tCCDebounce (100..200)              */
constexpr uint32_t T_FIRST_CAPS_MS        = 100;    /* VBUS on to first Source_Capabilities (<= 250) */
constexpr uint32_t T_SEND_SOURCE_CAP_MS   = 150;    /* tTypeCSendSourceCap (100..200)      */
constexpr uint32_t T_SENDER_RESPONSE_MS   = 30;     /* tSenderResponse                     */
constexpr uint32_t T_SRC_TRANSITION_MS    = 30;     /* tSrcTransition (25..35): Accept to the change */
constexpr uint32_t T_PPS_TIMEOUT_MS       = 15000;  /* tPPSTimeout                         */
constexpr uint32_t T_SRC_RECOVER_MS       = 800;    /* tSrcRecover (660..1000): VBUS off after Hard Reset */
constexpr uint32_t T_RECEIVE_US           = 1100;   /* tReceive                            */
constexpr uint32_t T_DETACH_POLL_MS       = 20;
constexpr uint8_t  N_RETRY                = 2;      /* nRetryCount                         */
constexpr uint8_t  N_CAPS                 = 50;     /* nCapsCount                          */
constexpr uint8_t  N_HARD_RESET           = 2;      /* nHardResetCount                     */
constexpr uint8_t  N_DETACH               = 3;

enum : uint8_t {
    ST_OFF = 0,          /* before begin()                                          */
    ST_DETACHED,         /* Rp on, looking for a sink's Rd                           */
    ST_ATTACH_WAIT,      /* Rd seen; debouncing                                      */
    ST_STARTUP,          /* VBUS on; the first capabilities are due                  */
    ST_SEND_CAPS,        /* Source_Capabilities out, waiting for its GoodCRC         */
    ST_WAIT_REQUEST,     /* acknowledged; the sink's Request is due (tSenderResponse) */
    ST_ACCEPTED,         /* Accept out; the change follows tSrcTransition after it   */
    ST_READY,            /* explicit contract                                        */
    ST_IDLE,             /* attached, no contract: the sink went quiet after a Reject */
    ST_NO_PD,            /* attached, the sink never acknowledged: Type-C only       */
    ST_HARD_RESET,       /* VBUS off for tSrcRecover, then back on                   */
    ST_ERROR,            /* a callback refused; waits for the sink to go away        */
};

enum : uint8_t { TX_NONE = 0, TX_OTHER, TX_CAPS, TX_ACCEPT, TX_PS_RDY };

struct {
    volatile uint8_t state;
    volatile uint32_t phase_ms;

    /* configuration */
    uint32_t pdo[PD_PDO_MAX];
    uint8_t count;
    pd_caps_t offer;                   /* pdo[] read back, for checking Requests */
    PDRpCurrent rp;
    bool unconstrained;
    arduino::CH32RVUsbPdSource::VbusCallback vbus_cb;
    arduino::CH32RVUsbPdSource::VoltageCallback volt_cb;
    PDSourceError error;

    /* attach */
    uint8_t line;                      /* 1 or 2 once attached, 0 none          */
    uint8_t cand_line;
    uint32_t last_detect_ms;
    uint8_t detach_lows;
    bool vbus_on;

    /* protocol layer */
    volatile uint8_t rev;
    volatile uint8_t tx_id;
    volatile int8_t rx_id;
    volatile uint8_t tx_stage;         /* 0 idle, 1 GoodCRC out, 2 message out, 3 hard reset out */
    volatile uint8_t staged_len;
    volatile uint8_t tx_len;
    volatile uint8_t tx_kind;
    volatile bool await_crc;
    volatile uint32_t tx_done_us;
    volatile uint8_t retries;
    volatile uint32_t last_rx_ms;

    /* policy */
    uint8_t caps_sent;
    uint8_t hard_resets;
    volatile bool hard_reset_rx;       /* the sink sent one: recover in poll()  */
    volatile pd_request_t want;        /* the Request being accepted            */
    volatile int8_t contract_idx;
    volatile uint16_t contract_mv, contract_ma;
    volatile bool contract_pps;
    volatile uint32_t last_request_ms;
} src;

__attribute__((aligned(4))) uint8_t rx_buf[FRAME_MAX];
__attribute__((aligned(4))) uint8_t tx_buf[FRAME_MAX];
__attribute__((aligned(4))) uint8_t crc_buf[4];

struct IrqLock {
    IrqLock() { ch32rv_irq_disable(CH32RV_USBPD_IRQ); }
    ~IrqLock() { ch32rv_irq_enable(CH32RV_USBPD_IRQ); }
};

void enter(uint8_t state)
{
    src.state = state;
    src.phase_ms = millis();
}

uint16_t rp_bits()
{
    return src.rp == PD_RP_3A0 ? CH32RV_UPD_CC_PU_330
         : src.rp == PD_RP_1A5 ? CH32RV_UPD_CC_PU_180
         : CH32RV_UPD_CC_PU_80;
}

/* The comparator level that separates a sink's Rd from a cable's Ra for the
 * Rp in use: Rd puts about 0.4 / 0.9 / 1.7 V on the line, Ra under 0.2 / 0.2
 * / 0.35 V. */
uint16_t rd_threshold()
{
    return src.rp == PD_RP_3A0 ? CH32RV_UPD_CC_CMP_66
         : src.rp == PD_RP_1A5 ? (uint16_t)(3u << 5)          /* 0.45 V */
         : CH32RV_UPD_CC_CMP_22;
}

void cc_idle(volatile uint16_t &port)
{
    port = (uint16_t)(CH32RV_UPD_CC_CMP_66 | rp_bits());
}

/* One CC line has a sink's Rd: above the Ra level, and under the GPIO high
 * threshold (an open line is pulled to VDD by the Rp). */
bool line_has_rd(volatile uint16_t &port, uint8_t pin)
{
    port &= (uint16_t)~(CH32RV_UPD_CC_CMP_MASK | CH32RV_UPD_PA_CC_AI);
    port |= rd_threshold();
    delayMicroseconds(2);
    const bool above_ra = port & CH32RV_UPD_PA_CC_AI;
    cc_idle(port);
    return above_ra && digitalRead(pin) == LOW;
}

/* 1 or 2: the one line with Rd; 0 none, or both (not a sink). */
uint8_t which_line()
{
    const bool cc1 = line_has_rd(CH32RV_USBPD_PORT_CC1, CH32RV_USBPD_CC1_PIN);
    const bool cc2 = line_has_rd(CH32RV_USBPD_PORT_CC2, CH32RV_USBPD_CC2_PIN);
    return cc1 == cc2 ? 0 : cc1 ? 1 : 2;
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

void send_staged(uint8_t len, uint8_t kind)
{
    src.tx_len = len;
    src.tx_kind = kind;
    src.await_crc = true;
    src.retries = 0;
    if (src.tx_stage == 0) {
        src.tx_stage = 2;
        phy_send(tx_buf, len, CH32RV_UPD_SOP0);
    } else {
        src.staged_len = len;
    }
}

void put_header(uint8_t type, uint8_t count)
{
    const uint16_t header = pd_header_source(type, count, src.tx_id, src.rev);
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

void send_caps()
{
    put_header(PD_DATA_SOURCE_CAP, src.count);
    for (uint8_t i = 0; i < src.count; i++) {
        put_word((uint8_t)(2u + 4u * i), src.pdo[i]);
    }
    send_staged((uint8_t)(2u + 4u * src.count), TX_CAPS);
    enter(ST_SEND_CAPS);
}

void send_unsupported()
{
    send_control(src.rev >= PD_REV_3_0 ? PD_CTRL_NOT_SUPPORTED : PD_CTRL_REJECT);
}

void goodcrc(uint8_t their_id)
{
    const uint16_t header = pd_header_source(PD_CTRL_GOODCRC, 0, their_id, src.rev);
    crc_buf[0] = (uint8_t)header;
    crc_buf[1] = (uint8_t)(header >> 8);
    src.tx_stage = 1;
    phy_send(crc_buf, 2, CH32RV_UPD_SOP0);
}

void protocol_reset()
{
    src.tx_id = 0;
    src.rx_id = -1;
    src.await_crc = false;
    src.tx_kind = TX_NONE;
    src.staged_len = 0;
}

void no_contract()
{
    src.contract_idx = -1;
    src.contract_mv = src.vbus_on ? 5000 : 0;
    src.contract_ma = 0;
    src.contract_pps = false;
}

/* VBUS off and the port back to looking for a sink. */
void to_detached()
{
    if (src.vbus_on) {
        src.vbus_cb(false);
        src.vbus_on = false;
    }
    protocol_reset();
    src.tx_stage = 0;
    src.line = 0;
    src.detach_lows = 0;
    no_contract();
    cc_idle(CH32RV_USBPD_PORT_CC1);
    cc_idle(CH32RV_USBPD_PORT_CC2);
    enter(ST_DETACHED);
    rx_mode();
}

void hard_reset_out()
{
    protocol_reset();
    src.hard_resets++;
    src.tx_stage = 3;
    phy_send(tx_buf, 0, CH32RV_UPD_HARD_RESET);
    enter(ST_HARD_RESET);
}

/* A Request arrived (handler): Accept or Reject now; the change itself is
 * poll()'s, tSrcTransition after the Accept went out. */
void on_request()
{
    const uint32_t rdo = (uint32_t)rx_buf[2] | ((uint32_t)rx_buf[3] << 8) | ((uint32_t)rx_buf[4] << 16) |
                         ((uint32_t)rx_buf[5] << 24);
    pd_request_t r;
    src.last_request_ms = millis();
    if (!pd_check_request(&src.offer, rdo, &r)) {
        send_control(PD_CTRL_REJECT);
        /* The old contract stands; without one, the sink tries again. */
        enter(src.contract_idx >= 0 ? ST_READY : ST_IDLE);
        return;
    }
    src.want.index = r.index;
    src.want.pps = r.pps;
    src.want.mv = r.mv;
    src.want.ma = r.ma;
    src.want.mismatch = r.mismatch;
    send_control(PD_CTRL_ACCEPT, TX_ACCEPT);
    enter(ST_ACCEPTED);
}

void on_control(uint8_t type)
{
    switch (type) {
    case PD_CTRL_GET_SOURCE_CAP:
        send_caps();
        break;
    case PD_CTRL_SOFT_RESET:
        protocol_reset();
        send_control(PD_CTRL_ACCEPT);
        /* The capabilities go out again once the Accept is acknowledged. */
        src.caps_sent = 0;
        enter(ST_STARTUP);
        src.phase_ms = millis() - T_FIRST_CAPS_MS;
        break;
    case PD_CTRL_GET_SINK_CAP:   /* a source-only port has no sink capabilities */
    default:
        send_unsupported();
        break;
    case PD_CTRL_PING:
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
        if (src.await_crc && id == src.tx_id) {
            src.await_crc = false;
            src.tx_id = (uint8_t)((src.tx_id + 1u) & 0x7u);
            if (src.tx_kind == TX_CAPS) {
                src.hard_resets = 0;
                enter(ST_WAIT_REQUEST);
            } else if (src.tx_kind == TX_ACCEPT && src.state == ST_ACCEPTED) {
                enter(ST_ACCEPTED);      /* tSrcTransition counts from here */
            }
        }
        rx_mode();
        return;
    }

    /* Follow the sink down to PD 2.0 if that is what it speaks. */
    src.rev = pd_header_rev(header) == PD_REV_2_0 ? PD_REV_2_0 : PD_REV_3_0;
    goodcrc(id);
    const bool soft = ndo == 0 && type == PD_CTRL_SOFT_RESET;
    if (!soft && (int8_t)id == src.rx_id) {
        return;
    }
    src.rx_id = (int8_t)id;

    if (pd_header_extended(header)) {
        send_unsupported();
        return;
    }
    if (ndo == 0) {
        on_control(type);
        return;
    }
    switch (type) {
    case PD_DATA_REQUEST:
        if (ndo == 1 && count >= 10u) {
            on_request();
        } else {
            send_unsupported();
        }
        break;
    case PD_DATA_BIST:
    case PD_DATA_ALERT:
        break;
    case PD_DATA_VENDOR_DEFINED:
        if (src.rev >= PD_REV_3_0) {
            send_unsupported();
        }
        break;
    default:
        send_unsupported();
        break;
    }
}

void poll()
{
    if (src.state == ST_OFF) {
        return;
    }
    const uint32_t now = millis();
    const uint32_t in_state = now - src.phase_ms;

    /* Detached: look for one sink's Rd, then debounce it. */
    if (src.state == ST_DETACHED || src.state == ST_ATTACH_WAIT) {
        if (now - src.last_detect_ms < T_DETECT_MS) {
            return;
        }
        src.last_detect_ms = now;
        const uint8_t line = which_line();
        if (src.state == ST_DETACHED) {
            if (line) {
                src.cand_line = line;
                enter(ST_ATTACH_WAIT);
            }
            return;
        }
        if (line != src.cand_line) {
            enter(ST_DETACHED);
            return;
        }
        if (in_state < T_CC_DEBOUNCE_MS) {
            return;
        }
        /* Attached: talk on that line, VBUS on. */
        IrqLock lock;
        src.line = line;
        if (line == 2) {
            CH32RV_USBPD_CONFIG |= CH32RV_UPD_CC_SEL;
        } else {
            CH32RV_USBPD_CONFIG &= (uint16_t)~CH32RV_UPD_CC_SEL;
        }
        if (!src.vbus_cb(true)) {
            src.error = PDSourceError::VbusFailed;
            enter(ST_ERROR);
            return;
        }
        src.vbus_on = true;
        protocol_reset();
        src.tx_stage = 0;
        src.rev = PD_REV_3_0;
        src.caps_sent = 0;
        src.hard_resets = 0;
        src.detach_lows = 0;
        no_contract();
        enter(ST_STARTUP);
        rx_mode();
        return;
    }

    IrqLock lock;

    /* Recovering from a Hard Reset (ours or the sink's): VBUS to 0, wait
     * tSrcRecover, back to 5 V, capabilities again. */
    if (src.hard_reset_rx) {
        src.hard_reset_rx = false;
        protocol_reset();
        src.tx_stage = 0;
        enter(ST_HARD_RESET);
    }
    if (src.state == ST_HARD_RESET) {
        if (src.tx_stage != 0) {
            return;                  /* our Hard Reset is still going out */
        }
        if (src.vbus_on) {
            src.vbus_cb(false);
            src.vbus_on = false;
            no_contract();
            src.phase_ms = now;
            return;
        }
        if (in_state < T_SRC_RECOVER_MS) {
            return;
        }
        if (!src.vbus_cb(true)) {
            src.error = PDSourceError::VbusFailed;
            enter(ST_ERROR);
            return;
        }
        src.vbus_on = true;
        no_contract();
        src.caps_sent = 0;
        enter(ST_STARTUP);
        rx_mode();
        return;
    }

    /* A message nobody acknowledged: twice more, then it depends on what it
     * was - capabilities are simply sent again later, anything else is a
     * protocol error and ends in a Hard Reset. */
    if (src.await_crc && src.tx_stage == 0 && micros() - src.tx_done_us > T_RECEIVE_US) {
        if (src.retries < N_RETRY) {
            src.retries++;
            src.tx_stage = 2;
            phy_send(tx_buf, src.tx_len, CH32RV_UPD_SOP0);
            return;
        }
        src.await_crc = false;
        if (src.tx_kind == TX_CAPS) {
            src.caps_sent++;
            enter(src.caps_sent >= N_CAPS ? ST_NO_PD : ST_STARTUP);
        } else if (src.hard_resets < N_HARD_RESET) {
            hard_reset_out();
        } else {
            enter(ST_NO_PD);
        }
        return;
    }

    switch (src.state) {
    case ST_STARTUP:
        /* First capabilities T_FIRST_CAPS_MS after VBUS, later ones every
         * tTypeCSendSourceCap. */
        if (src.tx_stage == 0 && !src.await_crc &&
            in_state >= (src.caps_sent ? T_SEND_SOURCE_CAP_MS : T_FIRST_CAPS_MS)) {
            send_caps();
        }
        break;
    case ST_WAIT_REQUEST:
        if (in_state > T_SENDER_RESPONSE_MS + 5u) {
            if (src.hard_resets < N_HARD_RESET) {
                hard_reset_out();
            } else {
                enter(ST_NO_PD);
            }
        }
        break;
    case ST_ACCEPTED:
        /* tSrcTransition after the Accept was acknowledged, the change; then
         * PS_RDY. A refused change is a Hard Reset back to vSafe5V. */
        if (!src.await_crc && src.tx_stage == 0 && in_state >= T_SRC_TRANSITION_MS) {
            const uint16_t mv = src.want.mv, ma = src.want.ma;
            const bool pps = src.want.pps;
            /* A PPS keepalive (the same Request again) changes nothing. */
            const bool same = src.contract_idx == (int8_t)src.want.index && src.contract_mv == mv &&
                              src.contract_ma == ma && src.contract_pps == pps;
            if (!same && !src.volt_cb(mv, ma, pps)) {
                src.error = PDSourceError::VoltageFailed;
                hard_reset_out();
                break;
            }
            src.contract_idx = (int8_t)src.want.index;
            src.contract_mv = mv;
            src.contract_ma = ma;
            src.contract_pps = pps;
            src.last_request_ms = now;
            send_control(PD_CTRL_PS_RDY, TX_PS_RDY);
            enter(ST_READY);
        }
        break;
    case ST_READY:
        if (src.contract_pps && now - src.last_request_ms > T_PPS_TIMEOUT_MS && src.tx_stage == 0) {
            hard_reset_out();
        }
        break;
    default:
        break;
    }

    /* Detach: the sink's Rd gone from our line a few polls in a row. Only
     * sampled while the line is quiet. */
    if (src.state != ST_DETACHED && src.state != ST_HARD_RESET && src.tx_stage == 0 && !src.await_crc &&
        now - src.last_detect_ms >= T_DETACH_POLL_MS && now - src.last_rx_ms > 5u) {
        src.last_detect_ms = now;
        const bool there = src.line == 2 ? line_has_rd(CH32RV_USBPD_PORT_CC2, CH32RV_USBPD_CC2_PIN)
                                         : line_has_rd(CH32RV_USBPD_PORT_CC1, CH32RV_USBPD_CC1_PIN);
        src.detach_lows = there ? 0 : (uint8_t)(src.detach_lows + 1u);
        if (src.detach_lows >= N_DETACH) {
            to_detached();
        }
    }
}

}  // namespace

static void source_irq(void);

namespace arduino {

void CH32RVUsbPdSource::setRpCurrent(PDRpCurrent current)
{
    src.rp = current;
}

bool CH32RVUsbPdSource::addFixed(uint16_t millivolts, uint16_t milliamps)
{
    if (src.state != ST_OFF || src.count >= PD_PDO_MAX || millivolts < 5000 || millivolts > 20000 ||
        milliamps > 5000) {
        return false;
    }
    src.pdo[src.count++] = pd_source_pdo_fixed(millivolts, milliamps, 0);
    return true;
}

bool CH32RVUsbPdSource::addPps(uint16_t minMillivolts, uint16_t maxMillivolts, uint16_t milliamps)
{
    if (src.state != ST_OFF || src.count >= PD_PDO_MAX || minMillivolts > maxMillivolts ||
        minMillivolts < 3300 || maxMillivolts > 21000 || milliamps > 5000) {
        return false;
    }
    src.pdo[src.count++] = pd_source_pdo_pps(minMillivolts, maxMillivolts, milliamps);
    return true;
}

void CH32RVUsbPdSource::clearProfiles()
{
    if (src.state == ST_OFF) {
        src.count = 0;
    }
}

void CH32RVUsbPdSource::setUnconstrainedPower(bool unconstrained)
{
    src.unconstrained = unconstrained;
}

void CH32RVUsbPdSource::onVbus(VbusCallback cb)
{
    src.vbus_cb = cb;
}

void CH32RVUsbPdSource::onSetVoltage(VoltageCallback cb)
{
    src.volt_cb = cb;
}

bool CH32RVUsbPdSource::begin()
{
    if (src.state != ST_OFF) {
        return true;
    }
    if (ch32rv_usbpd_irq_hook && ch32rv_usbpd_irq_hook != source_irq) {
        src.error = PDSourceError::Busy;
        return false;
    }
    if (!src.vbus_cb) {
        src.error = PDSourceError::NoVbusCallback;
        return false;
    }
    if (!src.volt_cb) {
        src.error = PDSourceError::NoVoltageCallback;
        return false;
    }
    if (src.count == 0) {
        src.error = PDSourceError::NoProfiles;
        return false;
    }
    pd_parse_source_caps(src.pdo, src.count, &src.offer);
    if (src.offer.pdo[0].kind != PD_SUPPLY_FIXED || src.offer.pdo[0].max_mv != 5000) {
        src.error = PDSourceError::BadProfiles;
        return false;
    }
    src.pdo[0] &= ~(uint32_t)PD_SRC_UNCONSTRAINED;
    if (src.unconstrained) {
        src.pdo[0] |= PD_SRC_UNCONSTRAINED;
    }
    pd_parse_source_caps(src.pdo, src.count, &src.offer);

    ch32rv_usbpd_irq_hook = source_irq;
    ch32rv_usbpd_phy_init();
    CH32RV_USBPD_CONFIG = CH32RV_UPD_PD_DMA_EN | CH32RV_USBPD_FILTER;
    CH32RV_USBPD_STATUS = CH32RV_UPD_IF_ALL;
    src.error = PDSourceError::None;
    src.vbus_on = false;
    src.hard_reset_rx = false;
    src.state = ST_DETACHED;     /* to_detached() leaves it here, VBUS off */
    to_detached();
    CH32RV_USBPD_CONFIG |= CH32RV_UPD_IE_RX_ACT | CH32RV_UPD_IE_RX_RESET | CH32RV_UPD_IE_TX_END;
    ch32rv_irq_enable(CH32RV_USBPD_IRQ);
    src.last_detect_ms = millis() - T_DETECT_MS;
    poll();
    return true;
}

void CH32RVUsbPdSource::end()
{
    if (src.state == ST_OFF) {
        return;
    }
    ch32rv_irq_disable(CH32RV_USBPD_IRQ);
    if (src.vbus_on) {
        src.vbus_cb(false);
        src.vbus_on = false;
    }
    CH32RV_USBPD_CONFIG = 0;
    CH32RV_USBPD_PORT_CC1 = 0;
    CH32RV_USBPD_PORT_CC2 = 0;
    ch32rv_clock_disable_at(CH32RV_USBPD_CLKEN_ADDR, CH32RV_USBPD_CLKEN_MASK);
    ch32rv_usbpd_irq_hook = nullptr;
    no_contract();
    src.state = ST_OFF;
}

bool CH32RVUsbPdSource::attached()
{
    poll();
    return src.vbus_on;
}

bool CH32RVUsbPdSource::ready()
{
    poll();
    return src.state == ST_READY;
}

uint8_t CH32RVUsbPdSource::profileCount() const
{
    return src.count;
}

pd_pdo_t CH32RVUsbPdSource::profile(uint8_t index) const
{
    pd_caps_t caps;
    pd_parse_source_caps(src.pdo, src.count, &caps);
    if (index >= caps.count) {
        pd_pdo_t none = {PD_SUPPLY_UNKNOWN, 0, 0, 0, 0, 0};
        return none;
    }
    return caps.pdo[index];
}

uint16_t CH32RVUsbPdSource::voltage() const { return src.vbus_on ? src.contract_mv : 0; }
uint16_t CH32RVUsbPdSource::current() const { return src.contract_ma; }
int8_t CH32RVUsbPdSource::contractProfile() const { return src.contract_idx; }
bool CH32RVUsbPdSource::pps() const { return src.contract_pps; }

void CH32RVUsbPdSource::maintain()
{
    poll();
}

PDSourceError CH32RVUsbPdSource::lastError() const
{
    return src.error;
}

void CH32RVUsbPdSource::irq()
{
    const uint8_t status = CH32RV_USBPD_STATUS;

    if (status & CH32RV_UPD_IF_RX_RESET) {
        CH32RV_USBPD_STATUS = CH32RV_UPD_IF_RX_RESET;
        src.hard_reset_rx = true;    /* VBUS is switched in poll(), not here */
        rx_mode();
        return;
    }

    if (status & CH32RV_UPD_IF_TX_END) {
        CH32RV_USBPD_STATUS = CH32RV_UPD_IF_TX_END;
        CH32RV_USBPD_PORT_CC1 &= (uint16_t)~CH32RV_UPD_CC_LVE;
        CH32RV_USBPD_PORT_CC2 &= (uint16_t)~CH32RV_UPD_CC_LVE;
        if (src.tx_stage == 1 && src.staged_len) {
            const uint8_t len = src.staged_len;
            src.staged_len = 0;
            src.tx_stage = 2;
            phy_send(tx_buf, len, CH32RV_UPD_SOP0);
            return;
        }
        if (src.tx_stage == 2) {
            src.tx_done_us = micros();
        }
        src.tx_stage = 0;
        rx_mode();
        return;
    }

    if (!(status & CH32RV_UPD_IF_RX_ACT)) {
        return;
    }
    CH32RV_USBPD_STATUS = CH32RV_UPD_IF_RX_ACT;
    src.last_rx_ms = millis();
    if ((status & CH32RV_UPD_BMC_AUX_MASK) != CH32RV_UPD_AUX_SOP0) {
        rx_mode();
        return;
    }
    const uint16_t count = CH32RV_USBPD_BMC_BYTE_CNT;
    if (count < 6u) {
        rx_mode();
        return;
    }
    on_message(count);
}

}  // namespace arduino

static void source_irq(void)
{
    USBPDSource.irq();
}

#else  /* no USBPD block brought up on this part - see usbpd_hw.h */

namespace {
PDSourceError source_error = PDSourceError::Unsupported;
}

namespace arduino {

void CH32RVUsbPdSource::setRpCurrent(PDRpCurrent) {}
bool CH32RVUsbPdSource::addFixed(uint16_t, uint16_t) { return false; }
bool CH32RVUsbPdSource::addPps(uint16_t, uint16_t, uint16_t) { return false; }
void CH32RVUsbPdSource::clearProfiles() {}
void CH32RVUsbPdSource::setUnconstrainedPower(bool) {}
void CH32RVUsbPdSource::onVbus(VbusCallback) {}
void CH32RVUsbPdSource::onSetVoltage(VoltageCallback) {}
bool CH32RVUsbPdSource::begin() { return false; }
void CH32RVUsbPdSource::end() {}
bool CH32RVUsbPdSource::attached() { return false; }
bool CH32RVUsbPdSource::ready() { return false; }
uint8_t CH32RVUsbPdSource::profileCount() const { return 0; }
pd_pdo_t CH32RVUsbPdSource::profile(uint8_t) const
{
    pd_pdo_t none = {PD_SUPPLY_UNKNOWN, 0, 0, 0, 0, 0};
    return none;
}
uint16_t CH32RVUsbPdSource::voltage() const { return 0; }
uint16_t CH32RVUsbPdSource::current() const { return 0; }
int8_t CH32RVUsbPdSource::contractProfile() const { return -1; }
bool CH32RVUsbPdSource::pps() const { return false; }
void CH32RVUsbPdSource::maintain() {}
PDSourceError CH32RVUsbPdSource::lastError() const { return source_error; }
void CH32RVUsbPdSource::irq() {}

}  // namespace arduino

#endif /* CH32RV_USBPD_BASE */

namespace arduino {

const char *CH32RVUsbPdSource::lastErrorName() const
{
    switch (lastError()) {
    case PDSourceError::None: return "None";
    case PDSourceError::Unsupported: return "Unsupported";
    case PDSourceError::Busy: return "Busy";
    case PDSourceError::NoVbusCallback: return "NoVbusCallback";
    case PDSourceError::NoVoltageCallback: return "NoVoltageCallback";
    case PDSourceError::NoProfiles: return "NoProfiles";
    case PDSourceError::BadProfiles: return "BadProfiles";
    case PDSourceError::VbusFailed: return "VbusFailed";
    case PDSourceError::VoltageFailed: return "VoltageFailed";
    }
    return "?";
}

}  // namespace arduino

arduino::CH32RVUsbPdSource USBPDSource;
