/* USART-backed HardwareSerial.
 *
 * Transmit is interrupt-driven through a ring buffer so that print() does not
 * block on the wire; receive is interrupt-driven into a second ring buffer.
 * Serial, Serial1 .. Serial4 are defined one per file (HardwareSerial0.cpp
 * ...), so a sketch links only the ports it names; the USARTs they run on are
 * bound in begin(). See ch32rv_instances.h for how names reach USARTs.
 */
#pragma once

#include "api/HardwareSerial.h"
#include "internal/ch32rv_bus_clock.h"
#include "internal/ch32rv_pins.h"
#include "internal/ch32rv_printf.h"
#include "internal/ch32rv_ringbuffer.h"
#include "internal/ch32rv_route.h"
#include "CH32RVCallback.h"
/* Not just for the pad names: CH32RV_SERIAL_COUNT below comes from the
 * variant, and this header has to work when it is included first.
 * HardwareSerial.cpp includes it before Arduino.h, and when the variant was
 * only reachable through Arduino.h that left SERIAL_PORT_MONITOR undefined for
 * exactly one translation unit - the one holding the printf() bridge, which
 * then compiled to `return 0` and made every stdio write to Serial a silent
 * no-op. */
#include "pins_arduino.h"
#include "internal/ch32rv_instances.h"

#include <stdint.h>

/* 64 bytes each, which is the AVR core's size and fits CH32V003's 2 KB of RAM
 * with room to spare. Raise it per sketch with a build option, e.g.
 *   arduino-cli compile --build-property build.extra_flags=-DCH32RV_SERIAL_RX_BUFFER_SIZE=256
 * or the same line in a sketch's build_opt.h. Every port the sketch uses grows,
 * so the cost is (RX + TX) x the number of ports it names.
 *
 * The ring keeps one slot unusable to tell empty from full, so a size of N
 * holds N-1 bytes. Sizes must be at least 2. */
#ifndef CH32RV_SERIAL_RX_BUFFER_SIZE
#define CH32RV_SERIAL_RX_BUFFER_SIZE 64
#endif
#ifndef CH32RV_SERIAL_TX_BUFFER_SIZE
#define CH32RV_SERIAL_TX_BUFFER_SIZE 64
#endif

#if CH32RV_SERIAL_RX_BUFFER_SIZE < 2 || CH32RV_SERIAL_TX_BUFFER_SIZE < 2
#error "CH32RV_SERIAL_{RX,TX}_BUFFER_SIZE must be at least 2 (one slot is the empty/full marker)"
#endif

namespace arduino {

/* What onReceiveError() reports, by ESP32's names (arduino-esp32 HardwareSerial).
 * UART_FIFO_OVF_ERROR is the USART's overrun (a byte came before the last was
 * read out of the data register), UART_BUFFER_FULL_ERROR the receive buffer
 * full (the byte was dropped), UART_FRAME_ERROR / UART_PARITY_ERROR a byte that
 * came with that error and was dropped, UART_BREAK_ERROR a frame error on an
 * all-zero byte (the line held low). UART_NOISE_ERROR is the USART's own and
 * not ESP32's: the three samples of a bit disagreed (STATR NE); the byte, their
 * majority, is kept. */
typedef enum {
    UART_NO_ERROR,
    UART_BREAK_ERROR,
    UART_BUFFER_FULL_ERROR,
    UART_FIFO_OVF_ERROR,
    UART_FRAME_ERROR,
    UART_PARITY_ERROR,
    UART_NOISE_ERROR
} hardwareSerial_error_t;

/* setHwFlowCtrlMode()'s modes, by ESP32's names and numbers (ESP-IDF's
 * uart_hw_flowcontrol_t): RTS paces what comes in, CTS what goes out. */
typedef enum {
    UART_HW_FLOWCTRL_DISABLE = 0,
    UART_HW_FLOWCTRL_RTS = 1,
    UART_HW_FLOWCTRL_CTS = 2,
    UART_HW_FLOWCTRL_CTS_RTS = 3
} SerialHwFlowCtrl;

/* setMode()'s modes, by ESP32's names and numbers (ESP-IDF's uart_mode_t).
 * UART_MODE_UART and UART_MODE_RS485_HALF_DUPLEX are what this core does; the
 * others are refused. */
typedef enum {
    UART_MODE_UART = 0,
    UART_MODE_RS485_HALF_DUPLEX = 1,
    UART_MODE_IRDA = 2,
    UART_MODE_RS485_COLLISION_DETECT = 3,
    UART_MODE_RS485_APP_CTRL = 4
} SerialMode;

/* takeErrors()'s two kinds besides STATR's ORE (bit 3), NE (bit 2), FE (bit 1)
 * and PE (bit 0): a frame error on an all-zero byte (in place of FE), and a
 * byte dropped because the receive buffer was full. */
#define CH32RV_SERIAL_MARK_BREAK (1u << 4)
#define CH32RV_SERIAL_MARK_FULL  (1u << 5)

class CH32RVHardwareSerial;
/* The modes beyond plain UART (HardwareSerialMode.cpp): hardware flow
 * control, RS485's driver enable, single-wire half duplex and the receive
 * events. Until a sketch uses one of them this pointer is never written, so
 * link-time optimisation drops every call through it: a sketch that does not
 * use them pays nothing for them. */
struct CH32RVSerialModeOps {
    void (*start)(CH32RVHardwareSerial &port);   /* end of begin(): the mode's registers and pads */
    void (*end)(CH32RVHardwareSerial &port);     /* in end(): the mode's pads let go */
    void (*irq)(CH32RVHardwareSerial &port, uint32_t base);   /* the whole interrupt, in place of irq()'s */
    void (*read)(CH32RVHardwareSerial &port);    /* after read() took a byte */
    void (*write)(CH32RVHardwareSerial &port);   /* before write() hands a byte on */
    void (*written)(CH32RVHardwareSerial &port); /* after it has */
};
extern const CH32RVSerialModeOps *ch32rv_serial_mode_ops;

/* One object is one port. Which USART it drives is decided in begin(), not
 * here: the object holds state only, and that state is all zero at reset, so a
 * port lives in .bss and costs nothing until begin() binds it. Each USART's
 * constants (base, clock, IRQ, remap field, routes) sit in a const table in
 * HardwareSerial.cpp, and a USART set to Disable for the board
 * (CH32RV_USARTn_MODE = 0) is not in that table at all.
 *
 * `number` is the name's position (Serial = 0, Serial1 = 1, ...), which is
 * also its row in the table of enabled USARTs: begin() with pins takes any free
 * USART those pins are a route of, this row first; begin() without pins takes
 * this row on its default pins (ch32rv_instances.h). */
class CH32RVHardwareSerial : public HardwareSerial {
public:
    explicit CH32RVHardwareSerial(uint8_t number) : _number(number) {}

    /* A baud the USART cannot reach from F_CPU (outside F_CPU / 65535 ..
     * F_CPU / 16, or 0) leaves the port closed, reopened or not: check
     * `if (!Serial1)` after begin() when the baud comes from elsewhere. */
    void begin(unsigned long baudrate) override { begin(baudrate, SERIAL_8N1); }
    void begin(unsigned long baudrate, uint16_t config) override;
    /* arduino-esp32's form: the pins (RX, TX) named in the same call. -1, -1
     * keeps the pins set by setPins() / setRoute(), or the default ones; a pair
     * that is no enabled USART's route leaves the port closed, as does invert
     * (no USART here inverts its lines). */
    void begin(unsigned long baudrate, uint16_t config, int rxPin, int txPin,
               bool invert = false);
    void end() override;

    int available(void) override;
    /* Room left in the transmit ring. Print declares it with a default of 0,
     * which is safe but useless: a sketch checking it to avoid blocking would
     * conclude the port is permanently full. */
    int availableForWrite(void) override;
    int peek(void) override;
    int read(void) override;
    void flush(void) override;
    size_t write(uint8_t c) override;
    using Print::write;
    /* AVR's HardwareSerial has these: Serial.write(0) picks the byte write. */
    inline size_t write(unsigned long n) { return write((uint8_t)n); }
    inline size_t write(long n) { return write((uint8_t)n); }
    inline size_t write(unsigned int n) { return write((uint8_t)n); }
    inline size_t write(int n) { return write((uint8_t)n); }

    /* arduino-esp32's Print::printf - see ch32rv_printf.h. */
    CH32RV_PRINTF_MEMBER

    operator bool() override { return _slot != 0; }

    /* The USART number this port is bound to, 0 while closed. */
    uint8_t peripheral(void) const;

    /* Choose the pins for the next begin(), or move an open port onto them.
     *
     * setRoute() names a route of this name's own row (the USART a begin()
     * without pins would take); setPins() names the pins, and any enabled
     * USART whose route they are will do. Both return false and change nothing when no enabled USART has
     * that route, and setPins() refuses a TX and an RX from different routes -
     * the hardware cannot do that. setPins() takes RX first, as on an ESP32.
     *
     * As on an ESP32, a pin given as -1 is left as it is: setPins(-1, -1,
     * cts, rts) names only CTS / RTS. RX and TX the same pin is single-wire
     * half duplex (ESP32's one-wire): the USART's TX pad of a route, open
     * drain (it needs a pull-up), the receiver joined to it inside. Unlike
     * ESP32's, what the port sends does not come back to its own receive
     * buffer: the USART does not receive while it sends (every family on the
     * bench, 2026-11-17). CTS and RTS are the
     * pins setHwFlowCtrlMode() uses (they have to be the USART's own CTS / RTS
     * pads on the route TX / RX are on) and RTS is the driver enable of
     * UART_MODE_RS485_HALF_DUPLEX (any pin). */
    bool setRoute(uint8_t route);
    bool setPins(int rxPin, int txPin, int ctsPin = -1, int rtsPin = -1);

    /* ESP32's hardware flow control. RTS: low while the receive buffer holds
     * fewer than `threshold` bytes (at most the buffer's CH32RV_SERIAL_RX_BUFFER_SIZE
     * - 1); from there the USART keeps the next byte in its data register and
     * lets RTS go high at its stop bit, until read() makes room. CTS: a byte
     * goes out only while CTS is low (the pad is pulled up, so a CTS left
     * unconnected holds every byte). The pads are the ones setPins() named, or
     * else the USART's own on the route TX / RX are on (the first, where pads
     * choose their function one by one). False, and nothing changed, where
     * the USART has no such pad on that route, or in RS485 mode. Before or
     * after begin(); it lasts until changed, across end() and begin(). */
    bool setHwFlowCtrlMode(SerialHwFlowCtrl mode = UART_HW_FLOWCTRL_CTS_RTS, uint8_t threshold = 64);

    /* UART_MODE_RS485_HALF_DUPLEX: the RTS pin of setPins() (any pin; false
     * without one) is the transceiver's driver enable - high from before the
     * first start bit of a write until the last stop bit has gone out (the
     * transmit-complete interrupt), low otherwise and after end(). No USART
     * here drives it in hardware: the core does, with the TC flag. Hardware
     * flow control is off in this mode. UART_MODE_UART goes back (the pin is
     * let go, an input). IrDA and the RS485 test modes are refused. */
    bool setMode(SerialMode mode);

    /* ESP32's receive event: function runs (after loop(), as onReceiveError()
     * does - ch32rv_deliver.h) when the receive buffer reaches setRxFIFOFull()'s
     * count, and when the line has been quiet for setRxTimeout()'s symbols
     * after a byte. onlyOnTimeout: the second only (a whole message at once).
     * Called only with bytes to read. nullptr removes it. */
    void onReceive(CH32RVCallback<void(void)> function, bool onlyOnTimeout = false);
    /* Symbols (whole frames, at the port's baud and format) of quiet that
     * make a receive timeout; 0 none (onReceive() then runs on the count
     * alone). The USART's IDLE flag marks the first; the rest are counted when
     * the event is delivered. Default 1. */
    bool setRxTimeout(uint8_t symbols);
    /* Bytes in the receive buffer that make onReceive() run (unless
     * onlyOnTimeout), 1 .. CH32RV_SERIAL_RX_BUFFER_SIZE - 1 (a larger value is
     * that; 0 is refused). Default half the buffer. */
    bool setRxFIFOFull(uint8_t fifoBytes);

    /* Called after loop() (with serialEvent()), once for each kind of receive
     * error that happened since, as ESP32's onReceiveError() reports them
     * (there from its UART event task). The interrupt only marks the kinds;
     * how many of each is not kept. nullptr removes it. Defined in
     * HardwareSerialError.cpp, so a sketch that never calls it links none of
     * the delivery (the interrupt's marks are one byte per port). */
    void onReceiveError(CH32RVCallback<void(hardwareSerial_error_t)> function);

    /* For the shared interrupt handler and serialEvent() only. */
    void irq(void);
    uint8_t number(void) const { return _number; }
    /* The receive errors since the last call, cleared: STATR's ORE / NE / FE /
     * PE bits and CH32RV_SERIAL_MARK_BREAK / _FULL. */
    uint8_t takeErrors(void);

private:
    friend struct CH32RVSerialMode;    /* HardwareSerialMode.cpp */

    bool bind(uint8_t index, const ch32rv_route_t &route);
    uint32_t base(void) const;
    void start(void);
    /* irq()'s two halves, shared with the modes' interrupt (both inline,
     * internal/ch32rv_serial_irq.h). */
    void serviceRx(uint32_t base, uint16_t status);
    void serviceTx(uint32_t base, uint16_t status);
    /* For HardwareSerialMode.cpp, out of line: the registers of the open
     * USART; the route and table index of the open port, or those the next
     * begin() takes (nullptr: none); move the port onto a route. */
    uint32_t registers(void) const;
    const ch32rv_route_t *plannedRoute(uint8_t &index) const;
    void useRoute(uint8_t index, const ch32rv_route_t *route);

    const uint8_t _number;
    /* 0 = closed; otherwise 1 + index into the USART table. */
    uint8_t _slot;
#if CH32RV_SERIAL_COUNT > 1
    /* The open USART's registers, set by bind(): the interrupt and write()
     * read it here instead of the table (an entry is 36 bytes, found by a
     * multiply). With one USART the table's entry is a constant (base()). */
    uint32_t _base;
#endif
    /* Route chosen by setPins() / setRoute() for the next begin(), already
     * looked up, so begin() itself never searches: a sketch that does not name
     * pins links no route table and no search. nullptr = the default. */
    uint8_t _want_index;
    const ch32rv_route_t *_want;
    uint8_t _tx_pin;
    uint8_t _rx_pin;
    uint16_t _config;
    /* BRR, remembered so a pin change can reopen the port exactly as it was.
     * Where USART1's PCLK2 is not the others' PCLK1 (APB2 halved, Arduino.h)
     * the baud is kept instead, and start() divides the instance's clock. */
#if CH32RV_PCLK1_HZ == CH32RV_PCLK2_HZ
    uint16_t _brr;
#else
    uint32_t _baud;
#endif
    /* What a received byte keeps: 7 data bits with parity are an 8-bit word
     * whose top bit is the parity, which read() must not return. */
    uint8_t _rx_mask = 0xFF;
    /* Receive errors since the last takeErrors(). */
    volatile uint8_t _errors;
    CH32RVRingBuffer<CH32RV_SERIAL_RX_BUFFER_SIZE> _rx;
    CH32RVRingBuffer<CH32RV_SERIAL_TX_BUFFER_SIZE> _tx;
};

}  // namespace arduino

/* As many names as the board has enabled USARTs (ch32rv_instances.h). */
#if CH32RV_SERIAL_COUNT >= 1
extern arduino::CH32RVHardwareSerial Serial;
#define SERIAL_PORT_MONITOR  Serial
#define SERIAL_PORT_HARDWARE Serial
#endif
#if CH32RV_SERIAL_COUNT >= 2
extern arduino::CH32RVHardwareSerial Serial1;
#define SERIAL_PORT_HARDWARE1 Serial1
#endif
#if CH32RV_SERIAL_COUNT >= 3
extern arduino::CH32RVHardwareSerial Serial2;
#define SERIAL_PORT_HARDWARE2 Serial2
#endif
#if CH32RV_SERIAL_COUNT >= 4
extern arduino::CH32RVHardwareSerial Serial3;
#define SERIAL_PORT_HARDWARE3 Serial3
#endif
#if CH32RV_SERIAL_COUNT >= 5
extern arduino::CH32RVHardwareSerial Serial4;
#define SERIAL_PORT_HARDWARE4 Serial4
#endif
#if CH32RV_SERIAL_COUNT >= 6
extern arduino::CH32RVHardwareSerial Serial5;
#define SERIAL_PORT_HARDWARE5 Serial5
#endif
#if CH32RV_SERIAL_COUNT >= 7
extern arduino::CH32RVHardwareSerial Serial6;
#define SERIAL_PORT_HARDWARE6 Serial6
#endif
#if CH32RV_SERIAL_COUNT >= 8
extern arduino::CH32RVHardwareSerial Serial7;
#define SERIAL_PORT_HARDWARE7 Serial7
#endif
#if CH32RV_SERIAL_COUNT >= 9
extern arduino::CH32RVHardwareSerial Serial8;
#define SERIAL_PORT_HARDWARE8 Serial8
#endif
#if CH32RV_SERIAL_COUNT >= 10
extern arduino::CH32RVHardwareSerial Serial9;
#define SERIAL_PORT_HARDWARE9 Serial9
#endif

/* Retarget printf()/puts()/stderr. The default is the monitor port above,
 * from its begin() on (ch32rv_get_stdout() is nullptr until then).
 *
 *   ch32rv_set_stdout(&SerialSDI);   // trace over the debug probe, no wiring
 *   ch32rv_set_stdout(nullptr);      // discard
 *
 * This moves *stdio* only. The name `Serial` is bound at compile time and does
 * not follow - a sketch that wants to print to SDI writes SerialSDI.println()
 * as usual. Anything deriving from Print works, so a library that provides a
 * USB CDC port plugs in the same way, without the core knowing it exists. */
void ch32rv_set_stdout(arduino::Print *out);
arduino::Print *ch32rv_get_stdout(void);
/* For the monitor port's begin(): becomes stdout unless the sketch chose one.
 * Weak, so a sketch without stdio links neither this nor the bridge. */
void ch32rv_stdout_offer(arduino::Print *port) __attribute__((weak));
