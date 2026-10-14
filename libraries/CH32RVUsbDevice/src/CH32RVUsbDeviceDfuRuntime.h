/* DFU runtime: a DFU interface with no endpoints, through which a host
 * (dfu-util -e, or ch32rv) asks the running sketch to reset into the factory
 * USB bootloader with a DFU_DETACH. Included by CH32RVUsbDevice.h.
 *
 * Added to a CDC device it costs 532 bytes of flash and 20 bytes of RAM on
 * X035 (measured, MS OS 2.0 descriptors for Windows included); the 1200 bps
 * touch needs a CDC port, this does not. */
#pragma once

class CH32RVUsbDeviceDfuRuntime : public CH32RVUsbDeviceFunction {
public:
    explicit CH32RVUsbDeviceDfuRuntime(CH32RVUsbDevice &device = USBDevice);

    /* Called on DFU_DETACH, before the reset. Return false to refuse (the
     * host still sees the request succeed; the device simply stays). */
    typedef bool (*DetachCallback)(void);
    void onDetach(DetachCallback cb) { _onDetach = cb; }

    /* For the USB stack's callback. */
    void detach();

private:
    DetachCallback _onDetach = nullptr;
};
