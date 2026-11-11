/* The CH32H41x UHSIF (universal high-speed interface): a clocked state machine
 * on a 16-bit parallel data bus, feeding RAM through socket DMA. Register for
 * register it is Cypress FX3's GPIF II and PIB (docs/h417-uhsif.ja.md); WCH
 * publishes no registers, only a binary library, so this one is our own,
 * built from measurements on CH32H417 (2026-11-11).
 *
 * Covered (and measured): the data lines as inputs, the internal clock
 * SYSCLK / (divider + 1) up to 400 MHz, the state machine, the data counter,
 * up to four threads each writing a ring of buffers, and an interrupt raised
 * by the state machine. Not covered: driving the bus (output), an external
 * clock, the control lines as outputs, 8- and 32-bit widths.
 *
 *     CH32RVUhsif uhsif;
 *     uhsif.begin(1);                          // SYSCLK / 2 = 200 MHz
 *     static const CH32RVUhsifState states[] = { ... };
 *     uhsif.setStates(states, 2, start);
 *     uhsif.setRing(0, ring, 16384, 8, 16384); // thread 0: 8 buffers in a row
 *     uhsif.onInterrupt([] { ... });
 *     uhsif.start();
 *
 * The data lines: DATA0-15 = PD10-PD15, PF0-PF2, PC6-PC9, PA13-PA15 (UHSIF
 * PORT16-31). They are read whatever their pin mode (input, an alternate
 * function or GPIO output); begin() leaves them alone.
 *
 * States and transitions: each clock, the machine leaves its state by its
 * right transition when `goRight` (a truth table over the state's four
 * inputs) is true, else by its left one. A transition names the state it
 * enters and the outputs that go with entering it (alpha, beta); a
 * transition into the state it leaves does not repeat alpha, so a machine
 * that samples every clock alternates between two states. start() enters the
 * first state through the `start` transition. Only one CH32RVUhsif may be
 * begun at a time (there is one UHSIF). */
#pragma once

#include <Arduino.h>

#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415)

/* The inputs a state can look at. */
enum class CH32RVUhsifInput : uint8_t {
    Ctrl0 = 0,             /* CTRL0..CTRL15: Ctrl0 + n */
    DataCountHit = 20,     /* the data counter is at its limit */
    DmaReady = 26,         /* the current thread's socket can take data */
    One = 28,              /* always 1 */
};

struct CH32RVUhsifTransition {
    uint8_t next;          /* the state it enters (index into the states given to setStates()) */
    uint8_t alpha;         /* CH32RVUhsif::SampleData ... */
    uint32_t beta;         /* CH32RVUhsif::Push | CountData | ... | CH32RVUhsif::thread(n) */
};

struct CH32RVUhsifState {
    CH32RVUhsifInput inputs[4];   /* a, b, c, d */
    /* Bit (a + 2b + 4c + 8d) set: leave by `right`. 0 = always left. */
    uint16_t goRight;
    CH32RVUhsifTransition left, right;
};

enum class CH32RVUhsifError : uint8_t {
    None,
    NotBegun,
    InvalidArgument,     /* a bad index, size or alignment */
    TooManyFunctions,    /* more than 16 different goRight tables */
    Running,             /* stop() first */
};

class CH32RVUhsif {
public:
    /* alpha */
    static constexpr uint8_t SampleData = 0x04;          /* latch the data lines */
    /* beta */
    static constexpr uint32_t Push = 1u << 7;            /* hand the latched sample to the thread's socket */
    static constexpr uint32_t CountData = 1u << 16;      /* data counter + 1 */
    static constexpr uint32_t ResetDataCount = 1u << 17; /* data counter to 0 */
    static constexpr uint32_t Interrupt = 1u << 18;      /* call the onInterrupt() callback */
    static constexpr uint32_t thread(uint8_t n) { return (uint32_t)(n & 3u) << 4; }

    static constexpr uint8_t MaxStates = 255;
    static constexpr uint8_t MaxRingBuffers = 16;        /* per thread */
    static constexpr uint16_t MaxBufferBytes = 0xFFF0;

    /* Clock the UHSIF from SYSCLK / (divider + 1), divider 0..63, and put it
     * in a known state: 16-bit bus, data lines in, control lines in. */
    bool begin(uint8_t divider);
    void end();
    uint32_t clockHz() const;

    /* Program the state machine (before start()). */
    bool setStates(const CH32RVUhsifState *states, uint8_t count, const CH32RVUhsifTransition &start);

    /* The data counter: +1 on CountData, 0 on ResetDataCount; DataCountHit
     * reads 1 once it reaches `limit` (it stays there until ResetDataCount).
     * A ResetDataCount is not yet seen by the next state's decision: let the
     * state a reset enters leave by `left` whatever the counter says. */
    bool setDataCounter(uint32_t limit);

    /* Thread `n` (0..3) writes `count` buffers of `bytes` each (a multiple of
     * 16, up to MaxBufferBytes) in turn, round and round, without the CPU:
     * buffer k at first + k * stride. Buffers in RAM the DMA reaches (globals
     * and statics), 16-byte aligned. A buffer change takes the socket about
     * 30 clocks at 200 MHz while it fetches the next descriptor: a machine
     * that needs every sample alternates two threads (see CH32RVLogicCapture). */
    bool setRing(uint8_t n, void *first, uint16_t bytes, uint8_t count, uint32_t stride);
    /* Bytes thread `n` has written since start() (wraps at 2^32). */
    uint32_t bytesWritten(uint8_t n) const;

    /* Called (from the interrupt) each time a transition with Interrupt is taken. */
    void onInterrupt(CH32RVCallback<void()> callback) { _onInterrupt = callback; }

    bool start();
    /* Stops by resetting the UHSIF, which also clears what begin(),
     * setStates(), setDataCounter() and setRing() set: do them again before
     * the next start(). The buffers keep what was written. */
    void stop();
    bool running() const { return _running; }

    CH32RVUhsifError lastError() const { return _error; }
    const char *lastErrorName() const;

    /* For the interrupt handler. */
    void isr();

private:
    bool fail(CH32RVUhsifError e)
    {
        _error = e;
        return false;
    }

    CH32RVCallback<void()> _onInterrupt;
    uint8_t _divider = 0;
    bool _begun = false, _running = false;
    uint8_t _threads = 0;                 /* bit n: setRing(n) done */
    uint8_t _firstDscr[4] = {};
    CH32RVUhsifError _error = CH32RVUhsifError::None;
};

#else
#error "CH32RVUhsif is for CH32H415 / H416 / H417"
#endif
