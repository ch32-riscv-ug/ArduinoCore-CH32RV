/* USB smart card reader (CCID, EspUsbDeviceCcid): one emulated slot whose
 * "card" the sketch answers - the host's PC/SC stack sees a reader, and
 * every APDU it sends arrives in onApdu(). Included by CH32RVUsbDevice.h. */
#pragma once

/* Low two bits of bStatus (the wire encoding). */
enum CH32RVUsbDeviceCcidIccStatus : uint8_t {
    CH32RV_USB_DEVICE_CCID_ICC_ACTIVE = 0,   /* card present and activated */
    CH32RV_USB_DEVICE_CCID_ICC_INACTIVE = 1, /* card present, not activated */
    CH32RV_USB_DEVICE_CCID_ICC_ABSENT = 2,   /* no card in the slot */
};

/* bError values this reader reports (CCID 1.1 table 6.2-2). */
enum CH32RVUsbDeviceCcidError : uint8_t {
    CH32RV_USB_DEVICE_CCID_ERROR_CMD_NOT_SUPPORTED = 0x00,
    CH32RV_USB_DEVICE_CCID_ERROR_BAD_SLOT = 0x05,
    CH32RV_USB_DEVICE_CCID_ERROR_HW_ERROR = 0xFB,
    CH32RV_USB_DEVICE_CCID_ERROR_XFR_OVERRUN = 0xFC,
    CH32RV_USB_DEVICE_CCID_ERROR_ICC_MUTE = 0xFE,
    CH32RV_USB_DEVICE_CCID_ERROR_CMD_ABORTED = 0xFF,
};

/* Longest ATR ISO 7816-3 allows. */
static constexpr size_t CH32RV_USB_DEVICE_CCID_MAX_ATR = 33;

/* Largest CCID message (10-byte header + data) received or sent; covers a
 * short APDU exchange (10 + 261). Each byte here is RAM twice over. */
#ifndef CH32RV_USB_DEVICE_CCID_BUFFER_SIZE
#define CH32RV_USB_DEVICE_CCID_BUFFER_SIZE 280
#endif

class CH32RVUsbDeviceCcid : public CH32RVUsbDeviceFunction {
public:
    /* Answer one exchange: write the response (including SW1 SW2) into
     * `response`, at most `capacity` bytes, and return its length; 0 fails the
     * command (ICC_MUTE). Runs inside the USB stack: no blocking. */
    typedef size_t (*ApduCallback)(const uint8_t *apdu, size_t length, uint8_t *response, size_t capacity);
    typedef ApduCallback EscapeCallback;
    /* The host activated (true) or deactivated (false) the card. */
    typedef void (*PowerCallback)(bool on);

    explicit CH32RVUsbDeviceCcid(CH32RVUsbDevice &device = USBDevice);

    /* Put a card in the slot, answering IccPowerOn with this ATR (2..33
     * bytes), and tell the host. */
    bool insertCard(const uint8_t *atr, size_t length);
    void removeCard();
    bool cardPresent() const { return _cardPresent; }
    bool cardPowered() const { return _cardPowered; }
    size_t atr(uint8_t *buffer, size_t capacity) const;

    void onApdu(ApduCallback cb) { _onApdu = cb; }
    void onEscape(EscapeCallback cb) { _onEscape = cb; }
    void onPower(PowerCallback cb) { _onPower = cb; }

    /* The host has the interface configured. */
    bool mounted() const;
    /* Diagnostics: messages answered, XfrBlock exchanges, the last message type. */
    uint32_t commandCount() const { return _commandCount; }
    uint32_t apduCount() const { return _apduCount; }
    uint8_t lastMessageType() const { return _lastMessageType; }

    /* For the USB stack's callbacks. */
    void handleMessage(const uint8_t *message, size_t length);
    void handleOverrun(uint8_t messageType, uint8_t slot, uint8_t sequence);
    void handleAbort(uint8_t slot, uint8_t sequence);
    void handleNotifyComplete();
    void handleBusAttached();
    void handleBusDetached();

protected:
    void usbReset() override;
    void usbConfigured(CH32RVUsbSpeed speed) override;
    bool usbControl(CH32RVUsbControlStage stage, const CH32RVUsbSetup &setup) override;
    void usbTransferred(uint8_t address, uint16_t length) override;

private:
    void writeHeader(uint8_t messageType, uint32_t dataLength, uint8_t slot, uint8_t sequence, uint8_t icc,
                     uint8_t commandStatus, uint8_t error, uint8_t parameter);
    void sendStatus(uint8_t messageType, uint8_t slot, uint8_t sequence, uint8_t commandStatus, uint8_t error,
                    uint8_t parameter);
    void sendData(uint8_t messageType, uint8_t slot, uint8_t sequence, const uint8_t *data, size_t length,
                  uint8_t parameter);
    uint8_t iccStatus() const;
    void notifySlotState();

    ApduCallback _onApdu = nullptr;
    EscapeCallback _onEscape = nullptr;
    PowerCallback _onPower = nullptr;
    uint8_t _atr[CH32RV_USB_DEVICE_CCID_MAX_ATR] = {};
    uint8_t _atrLength = 0;
    volatile bool _cardPresent = false;
    volatile bool _cardPowered = false;
    volatile bool _notifyPending = false;
    bool _aborted = false;
    uint32_t _commandCount = 0;
    uint32_t _apduCount = 0;
    uint8_t _lastMessageType = 0;
};
