/* DFU: a DFU interface with no endpoints, through which a host (ch32rv, or
 * dfu-util -e) asks the running sketch to reset into the factory USB ISP
 * bootloader with a DFU_DETACH - the same job as the 1200 bps touch, without
 * needing a CDC port. Included by CH32RVUsbDevice.h.
 *
 * Where the program runs from RAM and rewrites its own flash
 * (CH32RV_FLASH_SELF_WRITE: CH32H41x, whose USB bootloader is out of reach
 * of software) DFU_DETACH brings the device back in DFU mode instead, and
 * dfu-util -D writes the code flash: `dfu-util -d 1209:0001 -D sketch.bin`
 * (the .bin Arduino builds, flash offset 0), which resets into it at the end.
 *
 * Added to a CDC device it costs 532 bytes of flash and 20 bytes of RAM on
 * X035 (measured, MS OS 2.0 descriptors for Windows included); the 1200 bps
 * touch needs a CDC port, this does not. */
#pragma once

class CH32RVUsbDeviceDfuRuntime : public CH32RVUsbDeviceFunction {
public:
    explicit CH32RVUsbDeviceDfuRuntime(CH32RVUsbDevice &device = USBDevice);

    /* Called when DFU_DETACH arrives, inside the USB service (a provider:
     * the request waits for the answer). Return false to refuse: the request
     * is stalled, so the host sees it fail, and the device stays as it is.
     * Once accepted the device leaves whatever follows - end(), a bus reset,
     * SET_CONFIGURATION 0 (the contract agreed with EspUsbDevice v3). The
     * request's wValue and the descriptor's wDetachTimeOut do not change
     * that: the device detaches itself (WILL_DETACH). */
    typedef CH32RVCallback<bool(void)> DetachCallback;
    void onDetach(DetachCallback cb) { _onDetach = cb; }

    /* For the USB stack's callback: leave, once the request has been accepted. */
    void detach();
    /* The one CH32RVUsbDeviceConfig::dfuRuntime adds, unless the sketch
     * declared its own. Best effort: a device with no room left goes without. */
    static void attachDefault(CH32RVUsbDevice &device);

protected:
    bool usbControl(CH32RVUsbControlStage stage, const CH32RVUsbSetup &setup) override;

public:
    /* In DFU mode (after DFU_DETACH, CH32RV_FLASH_SELF_WRITE parts). */
    bool inDfuMode() const { return _dfuMode; }

    /* For the device task (ch32rv_usb_deferred). */
    void usbTask();

private:
    void setState(uint8_t state, uint8_t status);
    bool writeBlock(uint16_t length);
    DetachCallback _onDetach = nullptr;
    uint8_t _status[6] = {0, 0, 0, 0, 0, 0};    /* bStatus, bwPollTimeout (3), bState, iString */
    bool _dfuMode = false;
    volatile bool _enterDfu = false;
    uint8_t *_block = nullptr;                  /* one download block (wTransferSize) */
    uint32_t _offset = 0;                       /* where the next block goes, or is read from */
    uint32_t _erased = 0;                       /* flash erased from 0 up to here */
};
