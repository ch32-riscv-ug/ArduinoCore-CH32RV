/* CAN 2.0B on the chip's bxCAN, as the HardwareCAN of ArduinoCore-API (the
 * shape the official Arduino cores give it: <Arduino_CAN.h>, CAN, CanMsg):
 *
 *     #include <Arduino_CAN.h>
 *     CAN.begin(CanBitRate::BR_500k);                   // default pins: CH32RV_CAN1_RX / _TX
 *     uint8_t data[] = {1, 2, 3};
 *     CAN.write(CanMsg(CanStandardId(0x123), sizeof data, data));
 *     if (CAN.available()) { CanMsg m = CAN.read(); }
 *
 * The bus needs a transceiver (TJA1050, SN65HVD230 ...) on RX / TX. Every
 * frame is received (one filter that takes all); write() is false (0 / -1)
 * when the three transmit mailboxes are full. Received frames wait in the
 * hardware's three-deep FIFO and, from there, in eight places of RAM: move
 * them by calling available() or read() often enough (no interrupt).
 * setLoopback() before begin() checks the controller alone: loopback hands
 * every frame sent back to the receiver, and silent keeps TX recessive (no
 * transceiver or bus needed at all).
 *
 * V20x, V205, V30x, V4x7, L103 / M103, H41x (CAN2 as CAN1 on the parts that
 * have two). The CAN clock is the APB1 clock: bit rates it does not divide
 * exactly into 8 - 25 time quanta are refused. */
#pragma once

#include <Arduino.h>
#include "api/HardwareCAN.h"
#include "internal/ch32rv_route.h"

#if defined(CH32RV_CAN1_BASE)

namespace arduino {

class CH32RVCan : public HardwareCAN {
public:
    explicit CH32RVCan(uint8_t number) : _number(number) {}

    bool begin(CanBitRate const can_bitrate) override;
    /* Any rate the CAN clock divides into 8 - 25 time quanta (sample point
     * near 87.5 %): false otherwise. */
    bool begin(uint32_t bitrate);
    void end() override;
    /* 1 sent (queued in a mailbox), 0 when all three are full, -1 when not
     * begun. */
    int write(CanMsg const &msg) override;
    size_t available() override;
    CanMsg read() override;

    /* Before begin(): loopback (frames sent come back to the receiver) and
     * silent (TX held recessive: nothing reaches the bus). */
    void setLoopback(bool loopback, bool silent = true) { _loopback = loopback; _silent = silent; }
    /* A route of the CAN's pads (CH32RV_CANn_ROUTES), before begin(). */
    bool setPins(int rx, int tx);
    /* The error counters (ERRSR TEC / REC) and whether the controller is bus-off. */
    uint8_t txErrors();
    uint8_t rxErrors();
    bool busOff();

private:
    void drain();

    const uint8_t _number;
    uint32_t _base = 0;
    bool _loopback = false, _silent = false;
    int16_t _rx = -1, _tx = -1;
    uint32_t _remap = 0, _remap2 = 0;
    bool _routeSet = false;
    static const uint8_t QUEUE = 8;
    CanMsg _queue[QUEUE];
    uint8_t _head = 0, _count = 0;
};

}  // namespace arduino

extern arduino::CH32RVCan CAN;
#if defined(CH32RV_CAN2_BASE)
extern arduino::CH32RVCan CAN1;
#endif

#endif
