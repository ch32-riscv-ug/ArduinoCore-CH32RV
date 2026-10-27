/* SPI peripheral (slave) mode, one transfer per chip select:
 *
 *     CH32RVSPISlave slave;
 *     slave.begin(PA5, PA6, PA7, PA4);              // SCK, MISO, MOSI, CS
 *     static uint8_t tx[8] = {...}, rx[8];
 *     slave.queue(tx, rx, sizeof rx);               // armed for the next CS low .. high
 *     CH32RVSPISlave::Result r;
 *     if (slave.poll(r)) { ... r.rx, r.length ... } // from loop()
 *
 * A queued transfer is one chip select: from CS falling the slave sends tx
 * (then the fill byte once tx runs out) and keeps up to `size` bytes of what
 * the master sent; CS rising ends it, and its Result says how many bytes the
 * master clocked (more than `size` when the master went on; only `size` are
 * kept). Up to QUEUE_DEPTH transfers wait their turn. A chip select with
 * nothing queued sends the fill byte, keeps nothing and is counted in
 * missed(). A chip select with no clock in it uses nothing up.
 *
 * CS is any pin with an EXTI line, not the SPI's own NSS pad: the CS edge
 * interrupt selects the slave (SSM / SSI) and hands MISO to it, and on the
 * way out lets MISO go (a floating input), so other devices can share the
 * bus. The cost is time: the master has to leave a few microseconds after
 * CS falls before the first clock edge, and between transfers, for those
 * interrupts; and every byte is one interrupt, which bounds the clock (see
 * README.md for what was measured).
 *
 * Results: poll() takes them one by one; or onTransfer() names a callback,
 * which deliverEvents() runs for each (ch32rv_deliver.h) - not both. The rx
 * buffer of a result stays the sketch's; the slave writes into it only while
 * it is queued.
 *
 * Several slaves on different SPIs may run at once; an SPI held by an SPI
 * bus object (SPI.begin()) is not available, and the other way round. */
#pragma once

#include <Arduino.h>
#include <SPI.h>

#include "ch32rv_deliver.h"
#include "ch32rv_spi_claim.h"

class CH32RVSPISlave {
public:
    static constexpr uint8_t QUEUE_DEPTH = 4;

    struct Result {
        uint8_t *rx;        /* the buffer queue() was given */
        size_t length;      /* bytes the master clocked; at most `size` of them are in rx */
        size_t size;        /* what queue() was given */
    };
    typedef void (*TransferCallback)(const Result &result, void *arg);

    /* SCK, MISO, MOSI: one route of an SPI (as SPI.begin(sck, miso, mosi));
     * CS: any pin with an EXTI line. mode: SPI_MODE0..3, as the master's.
     * False, and nothing held, when the pins are not that. */
    bool begin(int sck, int miso, int mosi, int cs, uint8_t mode = SPI_MODE0, BitOrder order = MSBFIRST);
    /* Drops whatever is queued and gives the pins and the SPI back. */
    void end();

    /* Sent once the queued tx runs out, and for a chip select with nothing
     * queued. 0xFF until set. */
    void setFill(uint8_t fill);

    /* Arm one transfer: tx (nullptr: the fill byte throughout) and rx
     * (nullptr: keep nothing), `size` bytes each. False when the queue is
     * full or not begun. The buffers have to stay put until its Result. */
    bool queue(const uint8_t *tx, uint8_t *rx, size_t size);
    /* queue(), then wait for that transfer's chip select to end (timeout_ms
     * 0: no limit). The bytes the master clocked, or 0 on a timeout - the
     * transfer stays queued then. Not with onTransfer(). */
    size_t transfer(const uint8_t *tx, uint8_t *rx, size_t size, uint32_t timeout_ms = 0);

    /* The oldest finished transfer, if there is one. */
    bool poll(Result &out);
    /* Or each finished transfer to `cb`, from deliverEvents(). nullptr: back to poll(). */
    void onTransfer(TransferCallback cb, void *arg = nullptr);

    /* Queued and not yet finished (the running one included). */
    uint8_t numTransactionsInFlight() const;
    /* Finished and not yet taken. */
    uint8_t available() const;
    /* Chip selects with clocks in them but nothing queued. */
    uint32_t missed() const { return _missed; }
    /* Bytes the master sent before the last one was read (the interrupt was
     * too late; that byte is lost and the rest of the transfer shifts). */
    uint32_t overruns() const { return _overruns; }

    /* For the interrupt handlers. */
    void isr();
    void csEdge();

private:
    struct Slot {
        const uint8_t *tx;
        uint8_t *rx;
        size_t size;
    };

    void restart();
    static size_t deliver();

    ch32rv_spi_claim_t _spi = {};
    uint8_t _cs = 0, _mode = 0;
    BitOrder _order = MSBFIRST;
    bool _begun = false;
    uint8_t _fill = 0xFF;
    volatile bool _selected = false;

    Slot _armed[QUEUE_DEPTH] = {};
    volatile uint8_t _armed_head = 0, _armed_count = 0;
    Result _done[QUEUE_DEPTH] = {};
    volatile uint8_t _done_head = 0, _done_count = 0;

    /* The running transfer (a copy of the armed head, or none). */
    const Slot *_cur = nullptr;
    volatile size_t _tx_i = 0, _rx_i = 0;
    volatile uint32_t _missed = 0, _overruns = 0;

    TransferCallback _cb = nullptr;
    void *_cb_arg = nullptr;
};
