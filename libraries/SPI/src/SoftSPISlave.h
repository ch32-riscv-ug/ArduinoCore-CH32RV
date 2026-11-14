/* SPI peripheral (slave) mode on any pads, one transfer per chip select - the
 * API of CH32RVSPISlave (the hardware SPI's slave), bit by bit in software:
 *
 *     SoftSPISlave slave;
 *     slave.begin(PC5, PC7, PC6, PC4);              // SCK, MISO, MOSI, CS
 *     static uint8_t tx[8] = {...}, rx[8];
 *     slave.queue(tx, rx, sizeof rx);               // armed for the next CS low .. high
 *     SoftSPISlave::Result r;
 *     if (slave.poll(r)) { ... r.rx, r.length ... } // from loop()
 *
 * A queued transfer is one chip select: from CS falling the slave sends tx
 * (then the fill byte once tx runs out) and keeps up to `size` bytes of what
 * the master sent; CS rising ends it, and its Result says how many whole bytes
 * the master clocked (more than `size` when it went on; only `size` are kept).
 * Up to QUEUE_DEPTH transfers wait their turn. A chip select with nothing
 * queued sends the fill byte, keeps nothing and is counted in missed(). A chip
 * select with no clock in it uses nothing up.
 *
 * Two pin-change interrupts do it (attachInterrupt): CS on both edges, and SCK
 * on the edge the mode samples on, where the handler reads MOSI and puts the
 * next bit on MISO, which the master samples one edge later. So every bit is
 * one interrupt, and the clock is bounded by its latency: the handler has to
 * run within half a clock period of its edge (MOSI changes on the next one),
 * and the master has to leave that much after CS falls before the first edge
 * and after the last edge before CS rises. A clock edge missed shows as a chip
 * select whose clocks were not whole bytes, counted in overruns(). SCK and CS
 * need different EXTI lines (pin bits: PA1 and PB1 share line 1), and those
 * lines are the slave's while it runs. MISO is driven only while CS is low
 * and is a floating input otherwise, so other devices can share the bus.
 *
 * Results: poll() takes them one by one; or onTransfer() names a callback,
 * which deliverEvents() runs for each (ch32rv_deliver.h) - not both. The rx
 * buffer of a result stays the sketch's; the slave writes into it only while
 * it is queued. */
#pragma once

#include <Arduino.h>
#include <CH32RVCallback.h>

#include "SPIClass.h"

#include <stddef.h>
#include <stdint.h>

class SoftSPISlave {
public:
    static constexpr uint8_t QUEUE_DEPTH = 4;

    struct Result {
        uint8_t *rx;        /* the buffer queue() was given */
        size_t length;      /* whole bytes the master clocked; at most `size` of them are in rx */
        size_t size;        /* what queue() was given */
    };
    /* A function, or a lambda that captures a little ([this], a pointer). */
    typedef CH32RVCallback<void(const Result &result)> TransferCallback;

    /* SCK, MISO, MOSI, CS: any pads, SCK and CS on different EXTI lines.
     * MISO -1: receive only (nothing driven); MOSI -1: send only (rx keeps
     * zeros). mode: SPI_MODE0..3, as the master's. False, and nothing held,
     * when the pins are not that. */
    bool begin(int sck, int miso, int mosi, int cs, uint8_t mode = SPI_MODE0, BitOrder order = MSBFIRST);
    /* Drops whatever is queued and gives the pins back as inputs. */
    void end();

    /* Sent once the queued tx runs out, and for a chip select with nothing
     * queued. 0xFF until set. */
    void setFill(uint8_t fill) { _fill = fill; }

    /* Arm one transfer: tx (nullptr: the fill byte throughout) and rx
     * (nullptr: keep nothing), `size` bytes each. False when the queue is
     * full or not begun. The buffers have to stay put until its Result. */
    bool queue(const uint8_t *tx, uint8_t *rx, size_t size);
    /* queue(), then wait for that transfer's chip select to end (timeout_ms
     * 0: no limit). The bytes the master clocked, or 0 on a timeout - the
     * transfer stays queued then unless it had not begun. Not with
     * onTransfer(). */
    size_t transfer(const uint8_t *tx, uint8_t *rx, size_t size, uint32_t timeout_ms = 0);

    /* The oldest finished transfer, if there is one. */
    bool poll(Result &out);
    /* Or each finished transfer to `cb`, from deliverEvents(). nullptr: back to poll(). */
    void onTransfer(TransferCallback cb);

    /* Queued and not yet finished (the running one included). */
    uint8_t numTransactionsInFlight() const { return _armed_count; }
    /* Finished and not yet taken. */
    uint8_t available() const { return _done_count; }
    /* Chip selects with clocks in them but nothing queued. */
    uint32_t missed() const { return _missed; }
    /* Chip selects whose clocks were not whole bytes: an edge the interrupt
     * was too late for (or a master that stopped mid-byte). */
    uint32_t overruns() const { return _overruns; }

    /* For the pin-change handlers. */
    void sckEdge();
    void csEdge();

private:
    struct Slot {
        const uint8_t *tx;
        uint8_t *rx;
        size_t size;
    };

    static size_t deliver();
    void miso_bit(bool one);

    uint8_t _sck = 0, _cs = 0;
    /* MOSI and MISO as port and bit, looked up once; port 0xFF: none. */
    uint8_t _mosi_port = 0xFF, _mosi_bit = 0, _miso_port = 0xFF, _miso_bit = 0;
    bool _msb = true;
    bool _begun = false;
    uint8_t _fill = 0xFF;
    volatile bool _selected = false;

    Slot _armed[QUEUE_DEPTH] = {};
    volatile uint8_t _armed_head = 0, _armed_count = 0;
    Result _done[QUEUE_DEPTH] = {};
    volatile uint8_t _done_head = 0, _done_count = 0;

    /* The running transfer (the armed head, or none). */
    const Slot *_cur = nullptr;
    volatile size_t _rx_i = 0, _tx_i = 0;
    volatile uint32_t _bits = 0;
    uint8_t _in = 0, _out = 0, _mask = 0x80;
    volatile uint32_t _missed = 0, _overruns = 0;

    TransferCallback _cb = nullptr;
    SoftSPISlave *_next = nullptr;      /* the begun slaves, for deliver() */
};
