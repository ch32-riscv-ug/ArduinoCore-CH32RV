/* DFU download (USB DFU 1.1, EspUsbDeviceDfu in Download mode): the running
 * sketch takes a new image over EP0 alone - `dfu-util -D sketch.ino.bin` -
 * stages it after itself, checks it and restarts into it (the core's
 * ch32rv_update.h, the same engine as the Update library). Beside any other
 * class; it uses no endpoint. Included by CH32RVUsbDevice.h.
 *
 *     CH32RVUsbDeviceDfu dfu;              // before USBDevice.begin()
 *     dfu.onProgress([](size_t n) { ... });
 *
 * The interface is in DFU mode from the start (protocol 2: no detach first).
 * The default DFU runtime stays beside it (CH32RVUsbDeviceConfig::dfuRuntime),
 * so the Arduino upload through ch32rv's DFU_DETACH keeps working; dfu-util
 * then needs to be told which of the two to use, by this one's name:
 * `dfu-util -a Firmware -D sketch.ino.bin` (or config.dfuRuntime = false).
 * `ch32rv boot dfu flash` picks the DFU-mode interface by itself.
 *
 * Failures are DFU statuses, not stalls: a first block that is not an image
 * of this core is errWRITE (3), as are a page that does not read back and an
 * image past the staging area (errADDRESS, 8); an image that fails the final
 * checks is errVERIFY (7). The sketch keeps running unchanged in every case,
 * and the host's CLRSTATUS returns to idle. EspUsbDeviceDfu's numbering. */
#pragma once

enum class CH32RVUsbDfuStatus : uint8_t {
    Ok = 0x00,
    Target = 0x01,
    File = 0x02,
    Write = 0x03,
    Erase = 0x04,
    CheckErased = 0x05,
    Prog = 0x06,
    Verify = 0x07,
    Address = 0x08,
    NotDone = 0x09,
    Firmware = 0x0A,
    Vendor = 0x0B,
    UsbReset = 0x0C,
    PowerOnReset = 0x0D,
    Unknown = 0x0E,
    StalledPacket = 0x0F,
};

/* EspUsbDfuConfig's fields that apply here. */
struct CH32RVUsbDfuConfig {
    /* wTransferSize: the largest block of one DFU_DNLOAD, 64 to 4096. */
    uint16_t transferBytes = 1024;
    /* A block buffer of the sketch's, transferBytes or more; nullptr takes
     * one from the heap at the first download. */
    uint8_t *transferBuffer = nullptr;
    /* bwPollTimeout while a block is written, and while the image is checked. */
    uint32_t downloadPollMs = 1;
    uint32_t manifestPollMs = 250;
};

class CH32RVUsbDeviceDfu : public CH32RVUsbDeviceFunction {
public:
    typedef CH32RVCallback<void(size_t)> ProgressCallback;
    typedef CH32RVCallback<bool(void)> CompleteCallback;
    typedef CH32RVCallback<void(CH32RVUsbDfuStatus)> ErrorCallback;

    explicit CH32RVUsbDeviceDfu(CH32RVUsbDevice &device = USBDevice, const char *name = nullptr);

    /* Before USBDevice.begin(). False for a transferBytes out of range. */
    bool configure(const CH32RVUsbDfuConfig &config);
    const CH32RVUsbDfuConfig &configuration() const { return _config; }

    /* These run inside the USB service, while the host waits: return quickly. */
    /* Bytes of the image taken so far, after each block. */
    void onProgress(ProgressCallback cb) { _onProgress = cb; }
    /* The image passed its checks. Return false to refuse it (errVERIFY;
     * nothing is installed). */
    void onComplete(CompleteCallback cb) { _onComplete = cb; }
    /* The download failed with this status. */
    void onError(ErrorCallback cb) { _onError = cb; }
    /* true (the default): once the image is accepted, leave the bus and
     * restart into it. false: the sketch restarts when it chooses
     * (Update.restart() or CH32RV.restart() install it). */
    void restartWhenComplete(bool enable) { _restart = enable; }

    /* A download is under way. */
    bool downloading() const;
    /* The DFU state and status the host would read (bState, bStatus). */
    uint8_t state() const { return _status[4]; }
    CH32RVUsbDfuStatus status() const { return (CH32RVUsbDfuStatus)_status[0]; }

protected:
    bool usbControl(CH32RVUsbControlStage stage, const CH32RVUsbSetup &setup) override;
    void usbReset() override;

private:
    void setState(uint8_t state, CH32RVUsbDfuStatus status, uint32_t pollMs = 0);
    void failed(CH32RVUsbDfuStatus status);
    bool block(uint16_t length);
    void manifest();
    CH32RVUsbDfuConfig _config;
    ProgressCallback _onProgress = nullptr;
    CompleteCallback _onComplete = nullptr;
    ErrorCallback _onError = nullptr;
    uint8_t *_block = nullptr;
    uint8_t _status[6] = {0, 0, 0, 0, 2, 0};    /* bStatus, bwPollTimeout (3), bState (dfuIDLE), iString */
    bool _restart = true;
    bool _ownBlock = false;
};
