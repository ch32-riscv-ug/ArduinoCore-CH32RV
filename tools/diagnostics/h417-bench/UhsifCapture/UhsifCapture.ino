/* UHSIF capture without WCH's library (bench only, not part of the core; docs/h417-uhsif.ja.md): our own
 * register setup, from what the registers mean (they are Cypress FX3's GPIF II and PIB). Captures the
 * 16 data lines (UHSIF PORT16-31: PD10-PD15, PF0-PF2, PC6-PC9, PA13-PA15) on an internal clock into eight
 * 16 KB buffers in a ring. Nothing is driven: no clock out, every control line an input.
 * UART commands (PB7 RX / PB6 TX), one per line:
 *   start <div>    UHSIF clock = SYSCLK / (div + 1); start streaming
 *   stop
 *   rate           buffers in one second
 *   gotog <n> <w>  toggle PF0 (D6, an LED pin) n times with a busy wait w, then report
 *   edges / runs   D6 changes in the ring
 *   pat <hex16>    data pin Dn pulled up when bit n is 1, else down
 * plus UsbFallback's one-letter p / f / s. */
#include <CH32RVUsbDevice.h>
#include "UsbFallback.h"

#define REG(a) (*(volatile uint32_t *)(a))

/* The UHSIF blocks (CH32H417; RM's address table says 0x40013400, the library uses these). */
constexpr uint32_t GPIF = 0x40038000u;       /* 76 config registers (FX3 GPIF_CONFIG .. GPIF_BETA_DEASSERT) */
constexpr uint32_t FUNC = 0x40038180u;       /* 32 transition functions, 16-bit truth tables */
constexpr uint32_t WAVE_L = 0x4003A000u;     /* left-edge state descriptors, 16 bytes apart */
constexpr uint32_t WAVE_R = 0x4003B000u;     /* right-edge state descriptors */
constexpr uint32_t SOCK = 0x40039000u;       /* socket n at SOCK + 0x40 n */

constexpr uint32_t BUF_BYTES = 16384, BUFS = 8;
static uint8_t ring[BUFS * BUF_BYTES] __attribute__((aligned(32)));
static volatile uint32_t dscr[BUFS][8] __attribute__((aligned(32)));
static volatile uint32_t filled, lastCount, rearmMode = 1;   /* 1: clear SUSPEND too (what resumes a socket) */

static uint32_t sock(uint32_t n) { return SOCK + 0x40u * n; }

/* One state descriptor: next state, the four lambda inputs, the left function F0 (and the right one F1),
 * alpha (left) and beta outputs. */
static void edge(uint32_t base, uint32_t state, uint32_t next, uint32_t fa, uint32_t f0, uint32_t f1, uint32_t alpha,
                 uint32_t beta, bool valid)
{
    volatile uint32_t *w = (volatile uint32_t *)(base + 16u * state);
    const uint32_t one = 28;                                             /* lambda: logic 1 */
    w[0] = next | (fa << 8) | (one << 13) | (one << 18) | (one << 23) | ((f0 & 0xF) << 28);
    w[1] = (f0 >> 4) | (f1 << 1) | (alpha << 6) | ((beta & 0x3FF) << 22);
    w[2] = (beta >> 10) | (valid ? 0x80000000u : 0);
}

static void stopUhsif()
{
    REG(0xE000E180u + 4u * (145 / 32)) = 1u << (145 % 32);              /* IRQ 145 off */
    REG(0x4002102Cu) |= 1u << 18;                                         /* RCC_HBRSTR: UHSIF reset */
    REG(0x4002102Cu) &= ~(1u << 18);
}

static void startUhsif(uint32_t div)
{
    stopUhsif();
    REG(0x40021018u) |= 1u << 18;                                         /* RCC_HBPCENR: UHSIF clock */
    REG(0x40021030u) = (REG(0x40021030u) & ~0xFFu) | (div & 0x3F);        /* CFGR2: UHSIFSRC SYSCLK, UHSIFDIV */
    REG(0x40010000u) &= ~0x3C0u;                                          /* AFIO_PCFR1: default PORT / CLK map */
    for (uint32_t i = 0; i < 76; i++) REG(GPIF + 4u * i) = 0;
    REG(GPIF + 0x000) = (1u << 31) | (1u << 8) | (1u << 4);               /* ENABLE, SYNC, CLK_SOURCE internal */
    REG(GPIF + 0x004) = 1u << 2;                                          /* BUS_CONFIG: 16-bit */
    REG(GPIF + 0x00C) = 1u;                                               /* AD_CONFIG: data lines always inputs */
    REG(GPIF + 0x018) = 0x7800u;                                          /* INTR_MASK: the four threads' socket events */
    REG(GPIF + 0x12C) = 0xFFFFFFC1u;                                      /* BETA_DEASSERT, FX3's default */
    for (uint32_t i = 0; i < 32; i++) REG(FUNC + 4u * i) = 0;
    REG(FUNC + 4u * 2) = 0xAAAA;                                          /* F2: input A (DMA ready) */
    REG(FUNC + 4u * 3) = 0x5555;                                          /* F3: not A */
    for (uint32_t s = 0; s < 8; s++) { edge(WAVE_L, s, 0, 0, 0, 0, 0, 0, false); edge(WAVE_R, s, 0, 0, 0, 0, 0, 0, false); }
    /* States 1 and 2 alternate while the DMA is ready: alpha bit 2 latches the data lines (it acts on a change
     * of state, so a state looping on itself would latch once), beta 7 (PUSH_WQ) hands the sample to the DMA.
     * Not ready: state 3 waits, then goes back with the same outputs. */
    const uint32_t lam = 26, latch = 1u << 2, push = 1u << 7;
    edge(WAVE_L, 0, 1, lam, 2, 3, latch, push, true);  edge(WAVE_R, 0, 3, lam, 3, 0, 0, 0, true);
    edge(WAVE_L, 1, 2, lam, 2, 3, latch, push, true);  edge(WAVE_R, 1, 3, lam, 3, 0, 0, 0, true);
    edge(WAVE_L, 2, 1, lam, 2, 3, latch, push, true);  edge(WAVE_R, 2, 3, lam, 3, 0, 0, 0, true);
    edge(WAVE_L, 3, 1, lam, 2, 0, latch, push, true);
    /* DMA: buffer i is socket 4i (thread 0's sockets step by 4), each with one descriptor that chains to itself:
     * buffer address, sync 0, the descriptor number (address / 32), size (count << 16 | size). */
    for (uint32_t i = 0; i < BUFS; i++) {
        volatile uint32_t *d = dscr[i];
        const uint32_t num = ((uint32_t)(uintptr_t)d >> 5) & 0xFFFF;
        d[0] = (uint32_t)(uintptr_t)(ring + i * BUF_BYTES);
        d[1] = 0;
        d[2] = num | 0x01000000u;
        d[3] = (BUF_BYTES << 16) | BUF_BYTES;
        const uint32_t k = sock(4 * i);
        REG(k + 0x00) = num | 0x06010000u;            /* SCK_DSCR: descriptor, count 1, low mark 6 */
        REG(k + 0x04) = BUF_BYTES;                    /* SCK_SIZE */
        REG(k + 0x08) = 0;                            /* SCK_COUNT */
        REG(k + 0x14) = 0xC0;                         /* SCK_INTR_MASK: ERROR, TRANS_DONE */
        REG(k + 0x0C) |= 0x8C000000u;                 /* SCK_STATUS: GO_ENABLE, SUSP_EOP, SUSP_TRANS */
    }
    REG(GPIF + 0x0FC) = 0x80000480u;                  /* THREAD_CONFIG[0]: ENABLE, burst 4, WM_CFG, socket 0 */
    filled = 0;
    REG(GPIF + 0x118) |= 1;                           /* WAVEFORM_CTRL_STAT: VALID */
    REG(GPIF + 0x11C) |= 1;                           /* WAVEFORM_SWITCH */
    REG(0xE000E100u + 4u * (145 / 32)) = 1u << (145 % 32);              /* IRQ 145 on */
}

/* Thread 0's socket finished a buffer: point the thread at the next socket (+4, eight buffers), clear the
 * socket's interrupt, give the buffer back (count 0, GO_SUSPEND pulsed - the library's way to resume it). */
extern "C" __attribute__((interrupt)) void UHSIF_IRQHandler(void)
{
    const uint32_t intr = REG(GPIF + 0x014);
    if (!(intr & (1u << 11))) return;
    const uint32_t cfg = REG(GPIF + 0x0FC);
    const uint32_t s = cfg & 31, next = (s + 4) & 31;
    const uint32_t k = sock(s);
    const uint32_t si = REG(k + 0x10);
    if (!(si & 1)) REG(GPIF + 0x0FC) = (cfg & ~31u) | next;
    REG(k + 0x10) = si & ((rearmMode & 1) ? 0x1E5u : 0x1C1u);   /* write 1 to clear what fired */
    lastCount = REG(k + 0x08);
    filled++;
    REG(k + 0x08) = 0;
    if (rearmMode & 2) REG(k + 0x00) += 1u << 16;     /* one more descriptor available */
    REG(k + 0x0C) |= 1u << 30;
    REG(k + 0x0C) &= ~(1u << 30);
}

UsbFallback usbFallback(USBDevice);
static char line[64];
static size_t used;

static const uint8_t dpin[16] = {PD10, PD11, PD12, PD13, PD14, PD15, PF0, PF1, PF2, PC6, PC7, PC8, PC9, PA13, PA14, PA15};

static void command(char *c)
{
    if (!strncmp(c, "start ", 6)) {
        startUhsif(strtoul(c + 6, nullptr, 0));
        Serial.println("STARTED");
    } else if (!strncmp(c, "mode ", 5)) {
        rearmMode = strtoul(c + 5, nullptr, 0);
        Serial.printf("MODE %lu\n", (unsigned long)rearmMode);
    } else if (!strncmp(c, "rd ", 3)) {
        char *e;
        uint32_t a = strtoul(c + 3, &e, 16), n = *e ? strtoul(e, nullptr, 0) : 1;
        for (uint32_t i = 0; i < n; i++) {
            if (i % 8 == 0) Serial.printf("\nRD %08lx:", (unsigned long)(a + 4 * i));
            Serial.printf(" %08lx", (unsigned long)REG(a + 4 * i));
        }
        Serial.println();
    } else if (!strncmp(c, "wr ", 3)) {
        char *e;
        uint32_t a = strtoul(c + 3, &e, 16), v = strtoul(e, nullptr, 16);
        REG(a) = v;
        Serial.printf("WR %08lx = %08lx -> %08lx\n", (unsigned long)a, (unsigned long)v, (unsigned long)REG(a));
    } else if (!strcmp(c, "st")) {
        Serial.printf("filled %lu INTR %08lx MASK %08lx LAMBDA %08lx WCS %08lx THR0 %08lx PFIC %08lx\n", (unsigned long)filled,
                      (unsigned long)REG(GPIF + 0x14), (unsigned long)REG(GPIF + 0x18), (unsigned long)REG(GPIF + 0x10C),
                      (unsigned long)REG(GPIF + 0x118), (unsigned long)REG(GPIF + 0xFC), (unsigned long)REG(0xE000E110u));
        for (uint32_t i = 0; i < 3; i++) {
            const uint32_t k = sock(4 * i);
            Serial.printf(" SCK%lu dscr %08lx size %08lx count %08lx status %08lx intr %08lx mask %08lx\n", (unsigned long)(4 * i),
                          (unsigned long)REG(k), (unsigned long)REG(k + 4), (unsigned long)REG(k + 8), (unsigned long)REG(k + 12),
                          (unsigned long)REG(k + 16), (unsigned long)REG(k + 20));
        }
    } else if (!strcmp(c, "stop")) {
        stopUhsif();
        Serial.println("STOPPED");
    } else if (!strcmp(c, "rate")) {
        const uint32_t n0 = filled, t0 = micros();
        delay(1000);
        const uint32_t n1 = filled, t1 = micros();
        Serial.printf("RATE %lu buffers in %lu us, last %lu bytes\n", (unsigned long)(n1 - n0), (unsigned long)(t1 - t0),
                      (unsigned long)lastCount);
    } else if (!strncmp(c, "pat ", 4)) {
        const uint16_t v = (uint16_t)strtoul(c + 4, nullptr, 16);
        for (int i = 0; i < 16; i++) pinMode(dpin[i], (v >> i) & 1 ? INPUT_PULLUP : INPUT_PULLDOWN);
        uint16_t r = 0;
        for (int i = 0; i < 16; i++) r |= (uint16_t)(digitalRead(dpin[i]) ? 1u : 0u) << i;
        Serial.printf("PAT %04x reads %04x\n", v, r);
    } else if (!strncmp(c, "gotog ", 6)) {
        char *e;
        const uint32_t n = strtoul(c + 6, &e, 0), w = *e ? strtoul(e, nullptr, 0) : 0;
        volatile uint32_t *cfgl = (volatile uint32_t *)0x40011C00u;
        *cfgl = (*cfgl & ~0xFu) | 0x3u;
        const uint32_t t0 = micros();
        for (uint32_t i = 0; i < n; i++) {
            REG(0x40011C10u) = 1;
            for (volatile uint32_t k = 0; k < w; k++) {}
            REG(0x40011C14u) = 1;
            for (volatile uint32_t k = 0; k < w; k++) {}
        }
        const uint32_t t1 = micros();
        stopUhsif();                                   /* keep the toggles in the ring */
        *cfgl = (*cfgl & ~0xFu) | 0x8u;
        Serial.printf("GOTOG %lu toggles in %lu us\n", (unsigned long)n, (unsigned long)(t1 - t0));
    } else if (!strcmp(c, "edges") || !strcmp(c, "runs")) {
        stopUhsif();                                   /* freeze the ring */
        const uint16_t *q = (const uint16_t *)ring;
        uint32_t edges = 0, run = 1, mn = 0xFFFFFFFF, mx = 0, shown = 0;
        if (c[0] == 'r') Serial.print("RUNS");
        for (uint32_t i = 1; i < sizeof ring / 2; i++) {
            if (((q[i] ^ q[i - 1]) >> 6) & 1) {
                edges++;
                if (run < mn) mn = run;
                if (run > mx) mx = run;
                if (c[0] == 'r' && shown < 40) { Serial.printf(" %lu", (unsigned long)run); shown++; }
                run = 1;
            } else {
                run++;
            }
        }
        if (c[0] == 'r') Serial.println();
        else Serial.printf("EDGES %lu runs min %lu max %lu first %04x\n", (unsigned long)edges, (unsigned long)mn,
                           (unsigned long)mx, q[0]);
    } else {
        Serial.println("?");
    }
}

void setup()
{
    Serial.begin(115200, SERIAL_8N1, PB7, PB6);
    usbFallback.onUart = [](int ch) {
        if (ch == '\r') return;
        if (ch == '\n') { line[used] = 0; if (used) command(line); used = 0; return; }
        if (used + 1 < sizeof line) line[used++] = (char)ch;
    };
    CH32RVUsbDeviceConfig config;
    config.product = "UHSIF capture";
    usbFallback.begin(config);
    Serial.println("UHSIF CAPTURE READY");
}

void loop()
{
    usbFallback.poll();
}
