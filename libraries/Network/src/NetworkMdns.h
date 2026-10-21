/* An mDNS responder (multicast DNS, "name.local", and DNS-SD services), as
 * ESP32's MDNS. It answers on every interface the Network library
 * has started, and on the ones started later.
 *
 *   MDNS.begin("sensor");                  // sensor.local
 *   MDNS.addService("http", "tcp", 80);    // _http._tcp, for browsers and Bonjour / Avahi
 *   MDNS.addServiceTxt("http", "tcp", "path", "/");
 *
 * Up to 4 services, 96 bytes of TXT items each. No queries (queryHost /
 * queryService) yet. */
#pragma once
#include <Arduino.h>

class NetworkMdns {
public:
    bool begin(const char *hostName);
    void end();
    /* The services' instance name (default: the host name). Set before addService(). */
    bool setInstanceName(const char *name);
    /* service and proto with or without the leading underscore: ("http", "tcp") or ("_http", "_tcp"). */
    bool addService(const char *service, const char *proto, uint16_t port);
    bool addServiceTxt(const char *service, const char *proto, const char *key, const char *value);
};

extern NetworkMdns MDNS;
