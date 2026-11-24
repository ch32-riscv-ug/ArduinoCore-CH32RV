#include "Arduino_CAN.h"

#if defined(CH32RV_CAN1_BASE)

#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_registers.h"

using namespace arduino;

namespace {

/* bxCAN (WCH's names; every family here, ch32-device-data registers.csv). */
#define CAN_REG(b, o)   CH32RV_REG32((b) + (o))
#define CAN_CTLR(b)     CAN_REG(b, 0x000u)
#define CAN_STATR(b)    CAN_REG(b, 0x004u)
#define CAN_TSTATR(b)   CAN_REG(b, 0x008u)
#define CAN_RFIFO0(b)   CAN_REG(b, 0x00Cu)
#define CAN_ERRSR(b)    CAN_REG(b, 0x018u)
#define CAN_BTIMR(b)    CAN_REG(b, 0x01Cu)
#define CAN_TXMIR(b, n)  CAN_REG(b, 0x180u + 0x10u * (n))
#define CAN_TXMDTR(b, n) CAN_REG(b, 0x184u + 0x10u * (n))
#define CAN_TXMDLR(b, n) CAN_REG(b, 0x188u + 0x10u * (n))
#define CAN_TXMDHR(b, n) CAN_REG(b, 0x18Cu + 0x10u * (n))
#define CAN_RXMIR(b)    CAN_REG(b, 0x1B0u)
#define CAN_RXMDTR(b)   CAN_REG(b, 0x1B4u)
#define CAN_RXMDLR(b)   CAN_REG(b, 0x1B8u)
#define CAN_RXMDHR(b)   CAN_REG(b, 0x1BCu)
/* The filters: CAN1's block, shared by CAN2 from bank CAN2SB on. */
#define CAN_FCTLR       CAN_REG(CH32RV_CAN1_BASE, 0x200u)
#define CAN_FMCFGR      CAN_REG(CH32RV_CAN1_BASE, 0x204u)
#define CAN_FSCFGR      CAN_REG(CH32RV_CAN1_BASE, 0x20Cu)
#define CAN_FAFIFOR     CAN_REG(CH32RV_CAN1_BASE, 0x214u)
#define CAN_FWR         CAN_REG(CH32RV_CAN1_BASE, 0x21Cu)
#define CAN_FR1(k)      CAN_REG(CH32RV_CAN1_BASE, 0x240u + 8u * (k))
#define CAN_FR2(k)      CAN_REG(CH32RV_CAN1_BASE, 0x244u + 8u * (k))

constexpr uint32_t CTLR_INRQ = 1u << 0, CTLR_SLEEP = 1u << 1, CTLR_TXFP = 1u << 2, CTLR_ABOM = 1u << 6;
constexpr uint32_t STATR_INAK = 1u << 0, STATR_SLAK = 1u << 1;
constexpr uint32_t TSTATR_TME0 = 1u << 26;
constexpr uint32_t RFIFO_FMP = 3u, RFIFO_RFOM = 1u << 5;
constexpr uint32_t ERRSR_BOFF = 1u << 2;
constexpr uint32_t BTIMR_LBKM = 1u << 30, BTIMR_SILM = 1u << 31;
constexpr uint32_t TXMIR_TXRQ = 1u << 0, TXMIR_IDE = 1u << 2;   /* RTR (bit 1): no remote frames sent */
constexpr uint32_t FCTLR_FINIT = 1u << 0;
constexpr uint8_t CAN2_FIRST_BANK = 14;

struct CanDesc {
    uint32_t base;
    uint32_t clken_addr, clken_mask;
    uint8_t rx, tx;              /* the default route's pads */
    uint32_t remap_mask, remap_val, remap2_mask, remap2_val;
};

#ifndef CH32RV_CAN1_REMAP_MASK
#define CH32RV_CAN1_REMAP_MASK 0u
#define CH32RV_CAN1_REMAP_VAL 0u
#endif
#ifndef CH32RV_CAN1_REMAP2_MASK
#define CH32RV_CAN1_REMAP2_MASK 0u
#define CH32RV_CAN1_REMAP2_VAL 0u
#endif
#ifndef CH32RV_CAN1_RX
#define CH32RV_CAN1_RX CH32RV_ROUTE_NO_PIN
#define CH32RV_CAN1_TX CH32RV_ROUTE_NO_PIN
#endif
#if defined(CH32RV_CAN2_BASE)
#ifndef CH32RV_CAN2_REMAP_MASK
#define CH32RV_CAN2_REMAP_MASK 0u
#define CH32RV_CAN2_REMAP_VAL 0u
#endif
#ifndef CH32RV_CAN2_REMAP2_MASK
#define CH32RV_CAN2_REMAP2_MASK 0u
#define CH32RV_CAN2_REMAP2_VAL 0u
#endif
#ifndef CH32RV_CAN2_RX
#define CH32RV_CAN2_RX CH32RV_ROUTE_NO_PIN
#define CH32RV_CAN2_TX CH32RV_ROUTE_NO_PIN
#endif
#endif

const CanDesc cans[] = {
    {CH32RV_CAN1_BASE, CH32RV_CLKEN_CAN1_ADDR, CH32RV_CLKEN_CAN1_MASK, CH32RV_CAN1_RX, CH32RV_CAN1_TX,
     CH32RV_CAN1_REMAP_MASK, CH32RV_CAN1_REMAP_VAL, CH32RV_CAN1_REMAP2_MASK, CH32RV_CAN1_REMAP2_VAL},
#if defined(CH32RV_CAN2_BASE)
    {CH32RV_CAN2_BASE, CH32RV_CLKEN_CAN2_ADDR, CH32RV_CLKEN_CAN2_MASK, CH32RV_CAN2_RX, CH32RV_CAN2_TX,
     CH32RV_CAN2_REMAP_MASK, CH32RV_CAN2_REMAP_VAL, CH32RV_CAN2_REMAP2_MASK, CH32RV_CAN2_REMAP2_VAL},
#endif
};

#if defined(CH32RV_CAN1_ROUTES)
const ch32rv_route_t can1_routes[] = CH32RV_CAN1_ROUTES;
#endif
#if defined(CH32RV_CAN2_ROUTES)
const ch32rv_route_t can2_routes[] = CH32RV_CAN2_ROUTES;
#endif

/* Up to `ms` for a status bit to reach `want`. */
bool wait_for(uint32_t base, uint32_t bit, bool want, uint32_t ms = 100)
{
    const uint32_t t0 = millis();
    while (((CAN_STATR(base) & bit) != 0u) != want) {
        if (millis() - t0 >= ms) {
            return false;
        }
    }
    return true;
}

/* BTIMR for `bitrate` from `clock`: 8 - 25 time quanta a bit, the sample
 * point nearest 87.5 % (CiA's recommendation), SJW one quantum. 0 when the
 * clock divides into none of them exactly. */
uint32_t timing(uint32_t clock, uint32_t bitrate)
{
    if (!bitrate) {
        return 0;
    }
    /* 16 quanta first, then 17, 15, 18, 14, ...: the middle of 8 - 25 */
    for (uint32_t k = 0; k < 20u; k++) {
        const uint32_t tq = (k & 1u) ? 16u - (k + 1u) / 2u : 16u + k / 2u;
        if (tq < 8u || tq > 25u || clock % (bitrate * tq) != 0u) {
            continue;
        }
        const uint32_t brp = clock / (bitrate * tq);
        const uint32_t ts1 = (tq * 7u + 4u) / 8u - 1u;   /* sync + TS1 = 87.5 % of the bit */
        const uint32_t ts2 = tq - 1u - ts1;
        if (brp >= 1u && brp <= 1024u && ts1 >= 1u && ts1 <= 16u && ts2 >= 1u && ts2 <= 8u) {
            return ((ts2 - 1u) << 20) | ((ts1 - 1u) << 16) | (brp - 1u);
        }
    }
    return 0;
}

}  // namespace

bool CH32RVCan::setPins(int rx, int tx)
{
    const ch32rv_route_t *rows = nullptr;
    uint8_t count = 0;
#if defined(CH32RV_CAN1_ROUTES)
    if (_number == 1) {
        rows = can1_routes;
        count = (uint8_t)(sizeof can1_routes / sizeof can1_routes[0]);
    }
#endif
#if defined(CH32RV_CAN2_ROUTES)
    if (_number == 2) {
        rows = can2_routes;
        count = (uint8_t)(sizeof can2_routes / sizeof can2_routes[0]);
    }
#endif
    if (rx < 0 || tx < 0) {
        return false;
    }
    const uint8_t r = (uint8_t)CH32RV_PIN_RESOLVE(rx), t = (uint8_t)CH32RV_PIN_RESOLVE(tx);
    for (uint8_t i = 0; i < count; i++) {
        if (rows[i].pins[0] == r && rows[i].pins[1] == t) {
            _rx = r;
            _tx = t;
            _remap = rows[i].value;
            _remap2 = rows[i].value2;
            _routeSet = true;
            return true;
        }
    }
    return false;
}

bool CH32RVCan::begin(CanBitRate const can_bitrate)
{
    return begin((uint32_t)can_bitrate);
}

bool CH32RVCan::begin(uint32_t bitrate)
{
    end();
    const CanDesc &d = cans[_number - 1];
    const uint32_t btimr = timing(CH32RV_PCLK1_HZ, bitrate);
    const uint8_t rx = _routeSet ? (uint8_t)_rx : d.rx, tx = _routeSet ? (uint8_t)_tx : d.tx;
    if (!btimr || rx == CH32RV_ROUTE_NO_PIN || tx == CH32RV_ROUTE_NO_PIN) {
        return false;
    }
    const uint32_t remap = _routeSet ? _remap : d.remap_val, remap2 = _routeSet ? _remap2 : d.remap2_val;

    ch32rv_clock_enable_at(d.clken_addr, d.clken_mask);
#if defined(CH32RV_CAN2_BASE)
    if (_number == 2) {
        /* CAN2's filters are CAN1's registers: CAN1's clock runs too */
        ch32rv_clock_enable_at(CH32RV_CLKEN_CAN1_ADDR, CH32RV_CLKEN_CAN1_MASK);
    }
#endif
    ch32rv_clock_enable(AFIO);
    if (d.remap_mask) {
        CH32RV_AFIO_PCFR1 = (CH32RV_AFIO_PCFR1 & ~d.remap_mask) | remap;
    }
    if (d.remap2_mask) {
        CH32RV_AFIO_PCFR2 = (CH32RV_AFIO_PCFR2 & ~d.remap2_mask) | remap2;
    }
    ch32rv_pin_set_af(rx, remap);
    ch32rv_pin_set_af(tx, remap);
    const uint8_t rp = (uint8_t)CH32RV_PIN_PORT(rx), rb = (uint8_t)CH32RV_PIN_BIT(rx);
    const uint8_t tp = (uint8_t)CH32RV_PIN_PORT(tx), tb = (uint8_t)CH32RV_PIN_BIT(tx);
    ch32rv_gpio_clock_enable(rp);
    ch32rv_gpio_clock_enable(tp);
    ch32rv_gpio_set_config(rp, rb, CH32RV_GPIO_CFG_IN_PULL);
    ch32rv_gpio_set(rp, rb);                       /* pulled up: recessive with no transceiver */
    ch32rv_gpio_set_config(tp, tb, CH32RV_GPIO_CFG_AF_PP_50M);

    /* Out of sleep into initialisation. */
    _base = d.base;
    CAN_CTLR(_base) = (CAN_CTLR(_base) & ~CTLR_SLEEP) | CTLR_INRQ;
    if (!wait_for(_base, STATR_INAK, true) || !wait_for(_base, STATR_SLAK, false)) {
        _base = 0;
        return false;
    }
    /* Automatic bus-off recovery; mailboxes in the order written. */
    CAN_CTLR(_base) = CTLR_INRQ | CTLR_ABOM | CTLR_TXFP;
    CAN_BTIMR(_base) = btimr | (_loopback ? BTIMR_LBKM : 0u) | (_silent && _loopback ? BTIMR_SILM : 0u);

    /* One filter that takes every frame, into FIFO 0: a 32-bit mask of 0. */
    const uint8_t bank = _number == 2 ? CAN2_FIRST_BANK : 0;
    CAN_FCTLR |= FCTLR_FINIT;
#if defined(CH32RV_CAN2_BASE)
    CAN_FCTLR = (CAN_FCTLR & ~(0x3Fu << 8)) | ((uint32_t)CAN2_FIRST_BANK << 8);
#endif
    CAN_FWR &= ~(1u << bank);
    CAN_FMCFGR &= ~(1u << bank);                   /* mask mode */
    CAN_FSCFGR |= 1u << bank;                      /* 32 bits */
    CAN_FAFIFOR &= ~(1u << bank);                  /* FIFO 0 */
    CAN_FR1(bank) = 0;
    CAN_FR2(bank) = 0;
    CAN_FWR |= 1u << bank;
    CAN_FCTLR &= ~FCTLR_FINIT;

    /* Frames left in the FIFO from before (H417 keeps them through the
     * initialisation mode, 2026-11-18): let go of them. */
    for (uint8_t i = 0; i < 3u && (CAN_RFIFO0(_base) & RFIFO_FMP); i++) {
        CAN_RFIFO0(_base) |= RFIFO_RFOM;
        const uint32_t t0 = micros();
        while ((CAN_RFIFO0(_base) & RFIFO_RFOM) && micros() - t0 < 100u) {
        }
    }

    /* Normal mode: it joins the bus after 11 recessive bits. */
    CAN_CTLR(_base) &= ~CTLR_INRQ;
    if (!wait_for(_base, STATR_INAK, false, 200)) {
        _base = 0;
        return false;
    }
    _head = _count = 0;
    return true;
}

void CH32RVCan::end()
{
    if (!_base) {
        return;
    }
    CAN_CTLR(_base) |= CTLR_INRQ;
    wait_for(_base, STATR_INAK, true);
    CAN_CTLR(_base) |= CTLR_SLEEP;
    _base = 0;
}

int CH32RVCan::write(CanMsg const &msg)
{
    if (!_base) {
        return -1;
    }
    const uint32_t tstatr = CAN_TSTATR(_base);
    uint8_t box = 3;
    for (uint8_t n = 0; n < 3; n++) {
        if (tstatr & (TSTATR_TME0 << n)) {
            box = n;
            break;
        }
    }
    if (box == 3) {
        return 0;
    }
    const uint8_t len = msg.data_length > 8 ? 8 : msg.data_length;
    const uint32_t id = msg.isExtendedId() ? ((msg.getExtendedId() << 3) | TXMIR_IDE)
                                           : (msg.getStandardId() << 21);
    uint8_t b[8] = {};
    memcpy(b, msg.data, len);
    CAN_TXMDTR(_base, box) = len;
    CAN_TXMDLR(_base, box) = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    CAN_TXMDHR(_base, box) = (uint32_t)b[4] | (uint32_t)b[5] << 8 | (uint32_t)b[6] << 16 | (uint32_t)b[7] << 24;
    CAN_TXMIR(_base, box) = id | TXMIR_TXRQ;
    return 1;
}

/* The hardware FIFO's frames into the RAM queue (the oldest kept when it is
 * full: a frame left in the FIFO stays there until room is made). */
void CH32RVCan::drain()
{
    if (!_base) {
        return;
    }
    while ((CAN_RFIFO0(_base) & RFIFO_FMP) && _count < QUEUE) {
        const uint32_t mir = CAN_RXMIR(_base), mdtr = CAN_RXMDTR(_base);
        const uint32_t lo = CAN_RXMDLR(_base), hi = CAN_RXMDHR(_base);
        const uint8_t data[8] = {(uint8_t)lo, (uint8_t)(lo >> 8), (uint8_t)(lo >> 16), (uint8_t)(lo >> 24),
                                 (uint8_t)hi, (uint8_t)(hi >> 8), (uint8_t)(hi >> 16), (uint8_t)(hi >> 24)};
        const uint32_t id = (mir & TXMIR_IDE) ? (((mir >> 3) & CanMsg::CAN_EFF_MASK) | CanMsg::CAN_EFF_FLAG)
                                              : ((mir >> 21) & CanMsg::CAN_SFF_MASK);
        const uint8_t len = (uint8_t)(mdtr & 0xFu);
        _queue[(_head + _count) % QUEUE] = CanMsg(id, len > 8 ? 8 : len, data);
        _count++;
        /* Release the FIFO's output and wait for it to move: read again
         * before RFOM clears, the mailbox still holds this frame (H417 takes
         * long enough to read every frame twice, 2026-11-18). */
        CAN_RFIFO0(_base) |= RFIFO_RFOM;
        const uint32_t t0 = micros();
        while ((CAN_RFIFO0(_base) & RFIFO_RFOM) && micros() - t0 < 100u) {
        }
    }
}

size_t CH32RVCan::available()
{
    drain();
    return _count;
}

CanMsg CH32RVCan::read()
{
    drain();
    if (!_count) {
        return CanMsg();
    }
    const CanMsg m = _queue[_head];
    _head = (uint8_t)((_head + 1) % QUEUE);
    _count--;
    return m;
}

uint8_t CH32RVCan::txErrors() { return _base ? (uint8_t)(CAN_ERRSR(_base) >> 16) : 0; }
uint8_t CH32RVCan::rxErrors() { return _base ? (uint8_t)(CAN_ERRSR(_base) >> 24) : 0; }
bool CH32RVCan::busOff() { return _base && (CAN_ERRSR(_base) & ERRSR_BOFF); }

arduino::CH32RVCan CAN(1);
#if defined(CH32RV_CAN2_BASE)
arduino::CH32RVCan CAN1(2);
#endif

#endif
