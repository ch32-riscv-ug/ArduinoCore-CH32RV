/* A stand-in for CH32RVUsbDevice in the host test build: just the network
 * function's surface that the Network library uses, over the tap link.
 * Like the real one, sendFrame() runs the USB stack first, and received
 * frames arrive from inside it (ch32rv_usb_task()). The IP-layer calls are
 * declared exactly as in libraries/CH32RVUsbDevice/src/CH32RVUsbDeviceNet.h;
 * the Network library defines them. */
#pragma once
#include <Arduino.h>
#include "CH32RVUsbCallback.h"

class NetworkUsbNcm;

class CH32RVUsbDeviceNet {
public:
    typedef CH32RVCallback<void(const uint8_t *frame, size_t length)> FrameCallback;
    CH32RVUsbDeviceNet();
    const uint8_t *macAddress() const;
    void onStackFrame(FrameCallback cb) { _onFrame = cb; }
    typedef CH32RVCallback<bool()> ReceiveGate;
    void receiveGate(ReceiveGate gate) { _receiveGate = gate; }
    void resumeReceive() {}   /* the stand-in asks the gate before every frame */
    bool sendFrame(const uint8_t *data, size_t length, uint32_t timeoutMs = 10);
    bool linkUp() const;

    void ipConfig(IPAddress local, IPAddress gateway, IPAddress subnet);
    void dhcpServer(bool enable);
    void dhcpClient(bool enable);
    void dhcpAdvertiseGateway(bool enable);
    void dhcpDns(IPAddress dns);
    void defaultRoute(bool enable);
    bool beginNetwork();
    bool networkUp() const;
    IPAddress localIP() const;
    NetworkUsbNcm *networkInterface() const { return _network; }

    /* The stand-in's own: start the tap link, and deliver a frame as the USB stack would. */
    void begin();
    void handleFrame(const uint8_t *data, size_t length);
    bool receiveReady() { return !_receiveGate || _receiveGate(); }

private:
    FrameCallback _onFrame = nullptr;
    ReceiveGate _receiveGate = nullptr;
    NetworkUsbNcm *_network = nullptr;
};
