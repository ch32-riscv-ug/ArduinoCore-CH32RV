/* onAnyControlRequest(): the requests the stack observes wait here, in order,
 * for deliverEvents(). Its own file, so a sketch that does not observe
 * carries neither the queue nor its delivery. */
#include "CH32RVUsbDevice.h"
#include "CH32RVUsbDeviceInternal.h"

#include "ch32rv_deliver.h"

namespace {

const uint8_t QUEUE = 8;

CH32RVUsbDevice::ControlRequestObserver user;
CH32RVUsbDeviceControlRequestInfo queue[QUEUE];
uint8_t first, count;
uint32_t dropped;

void record(const CH32RVUsbDeviceControlRequestInfo &info)
{
    if (count == QUEUE) {
        dropped++;
        return;
    }
    queue[(uint8_t)(first + count) % QUEUE] = info;
    count++;
}

size_t deliver()
{
    size_t ran = 0;
    for (uint8_t n = count; n && count; n--) {
        const CH32RVUsbDeviceControlRequestInfo info = queue[first];
        first = (uint8_t)((first + 1) % QUEUE);
        count--;
        if (user) {
            user(info);
            ran++;
        }
    }
    return ran;
}

ch32rv_delivery_t delivery = CH32RV_DELIVERY_INIT(deliver);

}  // namespace

void CH32RVUsbDevice::onAnyControlRequest(ControlRequestObserver observer)
{
    user = observer;
    if (observer) {
        ch32rv_usb_observe = record;
        ch32rv_delivery_attach(&delivery);
    } else {
        ch32rv_usb_observe = nullptr;
        ch32rv_delivery_detach(&delivery);
        first = count = 0;
    }
}

uint32_t CH32RVUsbDevice::droppedControlRequests() const
{
    return dropped;
}
