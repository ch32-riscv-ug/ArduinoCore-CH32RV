/* Serial's onReceive() (HardwareSerial.h): ESP32's receive event. In a file of
 * its own so that only a sketch that calls it links the delivery round
 * (ch32rv_deliver.h); the state and the interrupt's part are
 * HardwareSerialMode.cpp's. Delivered after loop(), as onReceiveError() is. */
#include "HardwareSerial.h"
#include "internal/ch32rv_deliver.h"
#include "internal/ch32rv_serial_irq.h"

using namespace arduino;

#if CH32RV_SERIAL_COUNT > 0

namespace {

size_t deliver(void)
{
    return CH32RVSerialMode::deliver();
}

ch32rv_delivery_t delivery = CH32RV_DELIVERY_INIT(deliver);

}  // namespace

void CH32RVHardwareSerial::onReceive(CH32RVCallback<void(void)> function, bool onlyOnTimeout)
{
    CH32RVSerialMode::setOnReceive(*this, function, onlyOnTimeout);
    ch32rv_delivery_attach(&delivery);
}

#endif
