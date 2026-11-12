/* CH32RVUhsif.h. The registers are FX3's GPIF II and PIB at other addresses
 * (docs/h417-uhsif.ja.md): 76 configuration registers at 0x40038000 (the
 * block repeats every 0x200), transition functions at +0x180, the left and
 * right waveform memories at 0x4003A000 / 0x4003B000 (16 bytes a state),
 * and socket n at 0x40039000 + 0x40 n. What the fields do was measured on
 * CH32H417 (2026-11-11); the FX3 names are kept so the two can be compared. */
#include "CH32RVUhsif.h"

#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415)

#include "internal/ch32rv_registers.h"

#define GPIF          0x40038000u
#define GPIF_REG(off) CH32RV_REG32(GPIF + (off))
#define GPIF_CONFIG               GPIF_REG(0x000)
#define GPIF_BUS_CONFIG           GPIF_REG(0x004)
#define GPIF_AD_CONFIG            GPIF_REG(0x00C)
#define GPIF_INTR                 GPIF_REG(0x014)
#define GPIF_INTR_MASK            GPIF_REG(0x018)
#define GPIF_DATA_COUNT_CONFIG    GPIF_REG(0x094)
#define GPIF_DATA_COUNT_RESET     GPIF_REG(0x098)
#define GPIF_DATA_COUNT_LIMIT     GPIF_REG(0x09C)
#define GPIF_THREAD_CONFIG(n)     GPIF_REG(0x0FC + 4u * (n))
#define GPIF_WAVEFORM_CTRL_STAT   GPIF_REG(0x118)
#define GPIF_WAVEFORM_SWITCH      GPIF_REG(0x11C)
#define GPIF_BETA_DEASSERT        GPIF_REG(0x12C)
#define GPIF_FUNCTION(n)          CH32RV_REG32(0x40038180u + 4u * (n))
#define WAVE_LEFT                 0x4003A000u
#define WAVE_RIGHT                0x4003B000u
#define SOCK(n)                   (0x40039000u + 0x40u * (n))
#define SCK_DSCR(n)               CH32RV_REG32(SOCK(n) + 0x00u)
#define SCK_SIZE(n)               CH32RV_REG32(SOCK(n) + 0x04u)
#define SCK_COUNT(n)              CH32RV_REG32(SOCK(n) + 0x08u)
#define SCK_STATUS(n)             CH32RV_REG32(SOCK(n) + 0x0Cu)
#define SCK_INTR(n)               CH32RV_REG32(SOCK(n) + 0x10u)
#define SCK_INTR_MASK(n)          CH32RV_REG32(SOCK(n) + 0x14u)

#define RCC_HBPCENR   CH32RV_REG32(0x40021018u)
#define RCC_HBRSTR    CH32RV_REG32(0x4002102Cu)
#define RCC_CFGR2     CH32RV_REG32(0x40021030u)
#define RCC_UHSIF     (1u << 18)
#define AFIO_PCFR1    CH32RV_REG32(0x40010000u)
#define PCFR1_UHSIF_RM (0xFu << 6)                /* CLK_RM [7:6], PORT_RM [9:8]: 0 = DATA on PORT16-31 */

#define CONFIG_ENABLE          (1u << 31)
#define CONFIG_THREAD_IN_STATE (1u << 15)       /* the thread comes from beta, not THREAD_CONFIG */
#define CONFIG_SYNC            (1u << 8)
#define CONFIG_CLK_INTERNAL    (1u << 4)
#define BUS_16BIT              (1u << 2)
#define AD_DATA_ALWAYS_IN      1u
#define INTR_CPU               (1u << 1)         /* beta INTR_CPU */
#define DC_ENABLE_UP_BY_1      ((1u << 8) | (1u << 3) | 1u)   /* increment 1, up, enable; no reload */
#define THREAD_ENABLE_BURST4   0x80000480u       /* ENABLE, burst 2^4 words, WM_CFG */
/* GO_ENABLE, SUSP_EOP, SUSP_TRANS, TRUNCATE: the socket stops after each
 * buffer (SCK_SIZE = one buffer) and the interrupt sends it on to the next
 * descriptor. Left running on with no transfer size, a socket moved on 16
 * bytes before the end of every buffer and dropped the 8 samples meant for
 * them (measured: the phase of a PWM jumped by 8 samples at every buffer
 * boundary, and the last 16 bytes kept a marker written before the start). */
#define SCK_STATUS_PER_BUFFER  0x8C200000u
#define SCK_INTR_TRANS_DONE    (1u << 7)
#define GPIF_INTR_SOCKETS      (0xFu << 11)      /* a thread's socket raised an unmasked interrupt */
#define WAVE_VALID             (1u << 31)

/* A socket fetches its first descriptor after it is enabled; a machine
 * started before that is not ready and its first pushes go nowhere
 * (measured: started at once, it worked or not with the code layout; after
 * 100 us, always). */
#define SOCKET_SETTLE_US 100u

/* Descriptors: 32 bytes each, numbered by address / 32 (the socket reaches
 * descriptors by number). {buffer, sync, next, size}: next in [15:0] (the
 * upper half as the vendor sets it), size = bytes << 16 | bytes - the byte
 * count already full is what lets an ingress socket run without CPU
 * events. */
static volatile uint32_t dscrPool[4][CH32RVUhsif::MaxRingBuffers][8] __attribute__((aligned(32)));

static CH32RVUhsif *active;

static uint32_t dnum(const volatile uint32_t *d)
{
    return ((uint32_t)(uintptr_t)d >> 5) & 0xFFFFu;
}

static void resetBlock()
{
    ch32rv_irq_disable(CH32RV_IRQN_UHSIF);
    RCC_HBRSTR |= RCC_UHSIF;
    RCC_HBRSTR &= ~RCC_UHSIF;
}

/* One waveform entry: the state it enters, the outputs, and the decision the
 * entered state will make (its inputs and functions F0 = go left, F1 = go
 * right): this hardware reads a state's decision from the entry that entered
 * it. */
static void writeEntry(uint32_t base, uint32_t hwState, uint32_t next, const uint8_t in[4], uint32_t f0, uint32_t f1,
                       uint32_t alpha, uint32_t beta)
{
    volatile uint32_t *w = (volatile uint32_t *)(base + 16u * hwState);
    w[0] = next | ((uint32_t)in[0] << 8) | ((uint32_t)in[1] << 13) | ((uint32_t)in[2] << 18) | ((uint32_t)in[3] << 23) |
           ((f0 & 0xFu) << 28);
    w[1] = (f0 >> 4) | (f1 << 1) | (alpha << 6) | ((beta & 0x3FFu) << 22);
    w[2] = (beta >> 10) | WAVE_VALID;
    w[3] = 0;
}

/* The USBHS PLL from HSE with a 25 MHz reference, as the USB drivers start
 * it (internal/ch32rv_usbhs2.h); left running once on. */
#define RCC_CTLR_HSEON        (1u << 16)
#define RCC_CTLR_HSERDY       (1u << 17)
#define RCC_CTLR_USBHS_PLLON  (1u << 20)
#define RCC_CTLR_USBHS_PLLRDY (1u << 21)
#define RCC_PLLCFGR2          CH32RV_REG32(0x40021034u)

static bool waitCtlr(uint32_t bit)
{
    for (uint32_t t0 = millis(); !(CH32RV_RCC_CTLR & bit);) {
        if ((uint32_t)(millis() - t0) > 100u) {
            return false;
        }
    }
    return true;
}

static bool usbhsPllOn()
{
    if (CH32RV_RCC_CTLR & RCC_CTLR_USBHS_PLLRDY) {
        return true;
    }
    CH32RV_RCC_CTLR |= RCC_CTLR_HSEON;
    if (!waitCtlr(RCC_CTLR_HSERDY)) {
        return false;
    }
    CH32RV_RCC_CTLR &= ~RCC_CTLR_USBHS_PLLON;
    RCC_PLLCFGR2 &= ~0x00001F0Fu;                     /* HSE, 25 MHz reference, no input division */
    CH32RV_RCC_CTLR |= RCC_CTLR_USBHS_PLLON;
    return waitCtlr(RCC_CTLR_USBHS_PLLRDY);
}

uint32_t CH32RVUhsif::sourceHz(CH32RVUhsifClock source)
{
    return source == CH32RVUhsifClock::UsbhsPll ? 480000000u : CH32RV_CLOCK_SYSCLK_HZ;
}

bool CH32RVUhsif::begin(uint8_t divider, CH32RVUhsifClock source)
{
    if (divider > 63 || (source != CH32RVUhsifClock::Sysclk && source != CH32RVUhsifClock::UsbhsPll)) {
        return fail(CH32RVUhsifError::InvalidArgument);
    }
    end();
    if (source == CH32RVUhsifClock::UsbhsPll && !usbhsPllOn()) {
        return fail(CH32RVUhsifError::ClockFailed);
    }
    RCC_HBPCENR |= RCC_UHSIF;
    resetBlock();
    RCC_CFGR2 = (RCC_CFGR2 & ~0xFFu) | ((uint32_t)source << 6) | divider;   /* UHSIFSRC [7:6], UHSIFDIV [5:0] */
    AFIO_PCFR1 &= ~PCFR1_UHSIF_RM;
    for (uint32_t i = 0; i < 76; i++) {
        GPIF_REG(4u * i) = 0;
    }
    for (uint32_t i = 0; i < 32; i++) {
        GPIF_FUNCTION(i) = 0;
    }
    for (uint32_t s = 0; s < 256; s++) {
        for (uint32_t w = 0; w < 4; w++) {
            CH32RV_REG32(WAVE_LEFT + 16u * s + 4u * w) = 0;
            CH32RV_REG32(WAVE_RIGHT + 16u * s + 4u * w) = 0;
        }
    }
    GPIF_CONFIG = CONFIG_ENABLE | CONFIG_THREAD_IN_STATE | CONFIG_SYNC | CONFIG_CLK_INTERNAL;
    GPIF_BUS_CONFIG = BUS_16BIT;
    GPIF_AD_CONFIG = AD_DATA_ALWAYS_IN;
    GPIF_BETA_DEASSERT = 0xFFFFFFC1u;                 /* FX3's default, as the vendor sets it */
    _divider = divider;
    _source = source;
    _threads = 0;
    _begun = true;
    active = this;
    _error = CH32RVUhsifError::None;
    return true;
}

void CH32RVUhsif::end()
{
    stop();
    if (_begun || active == this) {
        resetBlock();
        RCC_HBPCENR &= ~RCC_UHSIF;
    }
    _begun = false;
    if (active == this) {
        active = nullptr;
    }
}

uint32_t CH32RVUhsif::clockHz() const
{
    return _begun ? sourceHz(_source) / (_divider + 1u) : 0;
}

bool CH32RVUhsif::setStates(const CH32RVUhsifState *states, uint8_t count, const CH32RVUhsifTransition &start)
{
    if (!_begun) {
        return fail(CH32RVUhsifError::NotBegun);
    }
    if (_running) {
        return fail(CH32RVUhsifError::Running);
    }
    if (!states || count == 0 || count > MaxStates || start.next >= count) {
        return fail(CH32RVUhsifError::InvalidArgument);
    }
    /* Functions: a pair (go left = not goRight, go right = goRight) for each
     * different table, 32 slots. */
    uint16_t tables[16];
    uint8_t used = 0;
    uint8_t fnOf[MaxStates];
    for (uint8_t s = 0; s < count; s++) {
        const CH32RVUhsifState &st = states[s];
        if (st.left.next >= count || st.right.next >= count) {
            return fail(CH32RVUhsifError::InvalidArgument);
        }
        for (uint8_t i = 0; i < 4; i++) {
            if ((uint8_t)st.inputs[i] > 31) {
                return fail(CH32RVUhsifError::InvalidArgument);
            }
        }
        uint8_t k = 0;
        while (k < used && tables[k] != st.goRight) {
            k++;
        }
        if (k == used) {
            if (used == 16) {
                return fail(CH32RVUhsifError::TooManyFunctions);
            }
            tables[used++] = st.goRight;
        }
        fnOf[s] = k;
    }
    for (uint8_t k = 0; k < used; k++) {
        GPIF_FUNCTION(2u * k) = (uint16_t)~tables[k];
        GPIF_FUNCTION(2u * k + 1u) = tables[k];
    }
    /* Hardware state 0 is the start; the sketch's state s is s + 1. */
    auto entry = [&](uint32_t base, uint32_t hw, const CH32RVUhsifTransition &t) {
        const CH32RVUhsifState &to = states[t.next];
        const uint8_t in[4] = {(uint8_t)to.inputs[0], (uint8_t)to.inputs[1], (uint8_t)to.inputs[2], (uint8_t)to.inputs[3]};
        writeEntry(base, hw, t.next + 1u, in, 2u * fnOf[t.next], 2u * fnOf[t.next] + 1u, t.alpha, t.beta);
    };
    entry(WAVE_LEFT, 0, start);
    entry(WAVE_RIGHT, 0, start);
    for (uint8_t s = 0; s < count; s++) {
        entry(WAVE_LEFT, s + 1u, states[s].left);
        entry(WAVE_RIGHT, s + 1u, states[s].right);
    }
    _error = CH32RVUhsifError::None;
    return true;
}

bool CH32RVUhsif::setDataCounter(uint32_t limit)
{
    if (!_begun) {
        return fail(CH32RVUhsifError::NotBegun);
    }
    if (_running) {
        return fail(CH32RVUhsifError::Running);
    }
    GPIF_DATA_COUNT_RESET = 0;
    GPIF_DATA_COUNT_LIMIT = limit;
    GPIF_DATA_COUNT_CONFIG = DC_ENABLE_UP_BY_1;
    return true;
}

bool CH32RVUhsif::setRing(uint8_t n, void *first, uint16_t bytes, uint8_t count, uint32_t stride)
{
    if (!_begun) {
        return fail(CH32RVUhsifError::NotBegun);
    }
    if (_running) {
        return fail(CH32RVUhsifError::Running);
    }
    const uint32_t base = (uint32_t)(uintptr_t)first;
    if (n > 3 || !first || (base & 31u) || bytes == 0 || (bytes % BufferUnit) || bytes > MaxBufferBytes || count == 0 ||
        count > MaxRingBuffers || (count > 1 && stride < bytes) || (stride & 31u)) {
        return fail(CH32RVUhsifError::InvalidArgument);
    }
    volatile uint32_t(*d)[8] = dscrPool[n];
    for (uint8_t k = 0; k < count; k++) {
        d[k][0] = base + k * stride;
        d[k][1] = 0;
        d[k][2] = 0x01000000u | dnum(d[(k + 1u) % count]);
        d[k][3] = ((uint32_t)bytes << 16) | bytes;
    }
    __asm__ volatile("fence rw, rw" ::: "memory");
    SCK_SIZE(n) = bytes;                               /* one buffer, then suspend */
    SCK_COUNT(n) = 0;
    SCK_INTR(n) = 0x3FFu;
    SCK_INTR_MASK(n) = SCK_INTR_TRANS_DONE;
    SCK_DSCR(n) = dnum(d[0]) | (1u << 16);
    SCK_STATUS(n) = SCK_STATUS_PER_BUFFER;
    GPIF_THREAD_CONFIG(n) = THREAD_ENABLE_BURST4 | n;
    _threads |= (uint8_t)(1u << n);
    _written[n] = 0;
    return true;
}

uint32_t CH32RVUhsif::bytesWritten(uint8_t n) const
{
    if (!_begun || n > 3 || !(_threads & (1u << n))) {
        return 0;
    }
    noInterrupts();
    const uint32_t b = _written[n] + SCK_COUNT(n);
    interrupts();
    return b;
}

bool CH32RVUhsif::start()
{
    if (!_begun) {
        return fail(CH32RVUhsifError::NotBegun);
    }
    if (_running) {
        return fail(CH32RVUhsifError::Running);
    }
    delayMicroseconds(SOCKET_SETTLE_US);
    GPIF_INTR = 0xFFFFFFFFu;
    GPIF_INTR_MASK = INTR_CPU | GPIF_INTR_SOCKETS;
    _running = true;
    GPIF_WAVEFORM_CTRL_STAT |= 1u;                    /* WAVEFORM_VALID */
    GPIF_WAVEFORM_SWITCH |= 1u;
    ch32rv_irq_enable(CH32RV_IRQN_UHSIF);
    return true;
}

void CH32RVUhsif::stop()
{
    if (!_running) {
        return;
    }
    /* A reset is the only stop that was measured to leave the sockets quiet;
     * the configuration goes with it, so begin() again before the next start(). */
    resetBlock();
    _running = false;
    _begun = false;
}

const char *CH32RVUhsif::lastErrorName() const
{
    switch (_error) {
    case CH32RVUhsifError::None:
        return "None";
    case CH32RVUhsifError::NotBegun:
        return "NotBegun";
    case CH32RVUhsifError::ClockFailed:
        return "ClockFailed";
    case CH32RVUhsifError::InvalidArgument:
        return "InvalidArgument";
    case CH32RVUhsifError::TooManyFunctions:
        return "TooManyFunctions";
    case CH32RVUhsifError::Running:
        return "Running";
    }
    return "?";
}

void CH32RVUhsif::isr()
{
    /* A socket that finished a buffer: clear what it raised (SUSPEND and
     * DSCR_IS_LOW among them, or it stays suspended), and pulse GO_SUSPEND
     * to send it on to its next descriptor. It must happen before the
     * thread comes back to it: the other thread is writing meanwhile. */
    for (uint8_t n = 0; n < 4; n++) {
        if (!(_threads & (1u << n))) {
            continue;
        }
        const uint32_t si = SCK_INTR(n);
        if (si & SCK_INTR_TRANS_DONE) {
            SCK_INTR(n) = si;
            _written[n] += SCK_COUNT(n);
            SCK_COUNT(n) = 0;
            SCK_STATUS(n) |= 1u << 30;
            SCK_STATUS(n) &= ~(1u << 30);
        }
    }
    const uint32_t intr = GPIF_INTR;
    GPIF_INTR = intr;
    if ((intr & INTR_CPU) && _onInterrupt) {
        _onInterrupt();
    }
}

extern "C" CH32RV_INTERRUPT void UHSIF_IRQHandler(void)
{
    if (active) {
        active->isr();
    } else {
        GPIF_INTR = 0xFFFFFFFFu;
    }
}

#endif
