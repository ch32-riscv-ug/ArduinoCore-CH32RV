/* UDP, as ESP32's NetworkUDP. Up to four received datagrams wait for
 * parsePacket(); more are dropped. A datagram to send is built between
 * beginPacket() and endPacket() (at most 1472 bytes: no fragmentation is
 * asked of the sender). */
#pragma once
#include <Arduino.h>

struct udp_pcb;
struct pbuf;

class NetworkUDP : public UDP {
public:
    NetworkUDP();
    ~NetworkUDP();
    NetworkUDP(const NetworkUDP &) = delete;
    NetworkUDP &operator=(const NetworkUDP &) = delete;

    uint8_t begin(IPAddress a, uint16_t p);
    uint8_t begin(uint16_t p) override;
    uint8_t beginMulticast(IPAddress a, uint16_t p) override;
    void stop() override;
    int beginMulticastPacket();
    int beginPacket() ;
    int beginPacket(IPAddress ip, uint16_t port) override;
    int beginPacket(const char *host, uint16_t port) override;
    int endPacket() override;
    size_t write(uint8_t) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    using Print::write;
    void flush() override {}
    int parsePacket() override;
    int available() override;
    int read() override;
    int read(unsigned char *buffer, size_t len) override;
    int read(char *buffer, size_t len) override { return read((unsigned char *)buffer, len); }
    int peek() override;
    void clear();
    IPAddress remoteIP() override;
    uint16_t remotePort() override;

private:
    friend struct NetworkUdpGlue;
    struct Datagram {
        struct pbuf *p;
        uint32_t ip;
        uint16_t port;
    };
    struct udp_pcb *_pcb = nullptr;
    Datagram _queue[4] = {};
    uint8_t _queued = 0;
    Datagram _current = {};      /* the one parsePacket() opened */
    uint16_t _offset = 0;
    uint8_t *_tx = nullptr;      /* the datagram being built */
    uint16_t _txLength = 0;
    uint32_t _txIp = 0, _multicast = 0;
    uint16_t _txPort = 0, _port = 0;
};
