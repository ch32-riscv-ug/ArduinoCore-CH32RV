/* Messages between CH32H41x's cores (ch32rv_multicore.h), on the IPC block (RM 4.4). A file of its own: linked,
 * with its two interrupt handlers, only when called.
 *
 * The IPC's channel n has 8 status bits (STS), their interrupt enables (ENA) and a control byte in CTLR: the core
 * its receive interrupt goes to (RX_CID), the receive interrupt's enable (RX_IER) and AUTOEN - a status bit set
 * through IPC_SET sets its ENA bit too. A status bit at 1 with its ENA bit at 1 raises the receive interrupt on
 * RX_CID's core, as the private interrupt IPC_CHn (16 + n; RM 4.7: the low 32 are per core). Here:
 *
 *   channel 0: to the V5F (core 1); status bit 0 = a message in MSG0
 *   channel 1: to the V3F (core 0); status bit 0 = a message in MSG1
 *
 * The sender waits for the bit to be 0 (the last message taken), writes MSG, then sets the bit; the receiver
 * reads MSG and clears the bit. ENA stays set after the first message (clearing STS leaves ENA, RM 4.4.1), and the
 * transmit interrupt (ENA 1 and STS 0) is never enabled. STS and MSG have no reset value (RM 4.4.2): the V5F sets
 * the two channels up and clears their bits once, before the V3F runs anything of the sketch (ch32rv_v3f.c calls
 * ch32rv_ipc_init() when this file is linked) or at its own first call. */
#include "ch32rv_multicore.h"

#if defined(CH32RV_HAS_IPC)

#include "Arduino.h"
#include "internal/ch32rv_interrupt.h"

#if defined(CH32RV_RUN_V5F)

#define IPC_CTLR  CH32RV_REG32(0xE000D000u)
#define IPC_ENA   CH32RV_REG32(0xE000D010u)
#define IPC_STS   CH32RV_REG32(0xE000D014u)
#define IPC_SET   CH32RV_REG32(0xE000D018u)
#define IPC_CLR   CH32RV_REG32(0xE000D01Cu)
#define IPC_MSG(n) CH32RV_REG32(0xE000D020u + 4u * (n))

/* CTLR's byte for a channel: RX_CID bit 2, RX_IER bit 5, AUTOEN bit 6 (TX_CID, TX_IER and LOCK left 0). */
#define IPC_RX_CID   (1u << 2)
#define IPC_RX_IER   (1u << 5)
#define IPC_AUTOEN   (1u << 6)
#define IPC_TO_V5F   0u     /* channel */
#define IPC_TO_V3F   1u
#define IPC_BIT(ch)  (1u << (8u * (ch)))      /* its status bit 0 */

static volatile uint8_t ready;
static void (*volatile handlers[2])(uint32_t);  /* by core number */

void ch32rv_ipc_init(void)
{
    if (ready || ch32rv_core_id() == 0u) {
        return;
    }
    const uint32_t lock = ch32rv_cores_lock();
    IPC_CTLR = (IPC_CTLR & 0xFFFF0000u) | (IPC_RX_CID | IPC_RX_IER | IPC_AUTOEN) << (8u * IPC_TO_V5F) |
               (IPC_RX_IER | IPC_AUTOEN) << (8u * IPC_TO_V3F);
    IPC_CLR = IPC_BIT(IPC_TO_V5F) | IPC_BIT(IPC_TO_V3F);
    IPC_ENA &= ~(IPC_BIT(IPC_TO_V5F) | IPC_BIT(IPC_TO_V3F));
    ready = 1;
    ch32rv_cores_unlock(lock);
}

/* The channel to this core and the one to the other. */
static inline uint32_t inbound(void)
{
    return ch32rv_core_id() ? IPC_TO_V5F : IPC_TO_V3F;
}

static inline uint32_t outbound(void)
{
    return ch32rv_core_id() ? IPC_TO_V3F : IPC_TO_V5F;
}

static void pause(void)
{
    for (int i = 0; i < 16; i++) {
        __asm__ volatile ("nop");
    }
}

int ch32rv_ipc_send(uint32_t message, uint32_t timeout_ms)
{
    ch32rv_ipc_init();
    const uint32_t ch = outbound();
    const uint32_t bit = IPC_BIT(ch);
    const uint32_t start = millis();
    for (;;) {
        /* Masked between the look and the set: a handler of this core's sending meanwhile would overwrite MSG. */
        const uint32_t irq = ch32rv_irq_save();
        if (!(IPC_STS & bit)) {
            IPC_MSG(ch) = message;
            __asm__ volatile ("fence iorw, iorw" ::: "memory");
            IPC_SET = bit;
            ch32rv_irq_restore(irq);
            return 1;
        }
        ch32rv_irq_restore(irq);
        if (timeout_ms != CH32RV_WAIT_FOREVER && (uint32_t)(millis() - start) >= timeout_ms) {
            return 0;
        }
        pause();
    }
}

/* Take the message waiting on channel ch, if any. */
static int take(uint32_t ch, uint32_t *message)
{
    const uint32_t bit = IPC_BIT(ch);
    if (!(IPC_STS & bit)) {
        return 0;
    }
    *message = IPC_MSG(ch);
    __asm__ volatile ("fence iorw, iorw" ::: "memory");    /* read before the sender may write the next */
    IPC_CLR = bit;
    return 1;
}

int ch32rv_ipc_receive(uint32_t *message)
{
    if (!message) {
        return 0;
    }
    ch32rv_ipc_init();
    const uint32_t irq = ch32rv_irq_save();
    const int got = take(inbound(), message);
    ch32rv_irq_restore(irq);
    return got;
}

int ch32rv_ipc_on_receive(void (*callback)(uint32_t message))
{
    ch32rv_ipc_init();
    const uint32_t core = ch32rv_core_id();
    const uint32_t irqn = core ? CH32RV_IRQN_IPC_CH0 : CH32RV_IRQN_IPC_CH1;
    handlers[core] = callback;
    if (callback) {
        ch32rv_irq_enable(irqn);          /* this core's own PFIC: the low 32 are private */
    } else {
        ch32rv_irq_disable(irqn);
    }
    return 1;
}

static void run(uint32_t ch)
{
    uint32_t message;
    if (take(ch, &message)) {
        void (*callback)(uint32_t) = handlers[ch32rv_core_id()];
        if (callback) {
            callback(message);
        }
    }
}

void IPC_CH0_Handler(void) CH32RV_INTERRUPT;
void IPC_CH0_Handler(void)
{
    run(IPC_TO_V5F);
}

void IPC_CH1_Handler(void) CH32RV_INTERRUPT;
void IPC_CH1_Handler(void)
{
    run(IPC_TO_V3F);
}

#else

/* CPU Core = V3F: the V5F stays asleep (crt0_ch32.S), so nothing is there to send to or hear from. */
int ch32rv_ipc_send(uint32_t message, uint32_t timeout_ms)
{
    (void)message;
    (void)timeout_ms;
    return 0;
}

int ch32rv_ipc_receive(uint32_t *message)
{
    (void)message;
    return 0;
}

int ch32rv_ipc_on_receive(void (*callback)(uint32_t message))
{
    (void)callback;
    return 0;
}

#endif
#endif
