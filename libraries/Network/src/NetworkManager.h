/* The stack as a whole, as ESP32's Network (Arduino core 3.x). */
#pragma once
#include <Arduino.h>
#include "NetworkInterface.h"

class NetworkManager : public Printable {
public:
    /* Starts lwIP. The other calls start it too; calling this is optional. */
    bool begin();
    /* A name or a dotted address. 1 and the address when it resolved, 0
     * otherwise. Waits up to timeoutMs (the stack keeps running meanwhile). */
    int hostByName(const char *aHostname, IPAddress &aResult, uint32_t timeoutMs = 10000);
    uint8_t *macAddress(uint8_t *mac);
    String macAddress();
    bool setDefaultInterface(NetworkInterface &ifc);
    NetworkInterface *getDefaultInterface();
    /* Some interface has an address. */
    bool isOnline();
    size_t printTo(Print &out) const override;

    /* The default host name for interfaces that were not given one ("" keeps
     * "ch32rv-" and the MAC's last three bytes). */
    static const char *getHostname();
    static bool setHostname(const char *hostname);
    static bool hostname(const String &aHostname) { return setHostname(aHostname.c_str()); }
};

extern NetworkManager Network;
