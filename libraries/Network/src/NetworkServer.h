/* A listening TCP port, as ESP32's NetworkServer. Connections the stack
 * accepted wait (up to max_clients) until accept() hands them out; one more
 * is refused. */
#pragma once
#include <Arduino.h>
#include "NetworkClient.h"

class NetworkServer {
public:
    NetworkServer(uint16_t port = 80, uint8_t max_clients = 4);
    NetworkServer(const IPAddress &addr, uint16_t port = 80, uint8_t max_clients = 4);
    ~NetworkServer();
    NetworkServer(const NetworkServer &) = delete;
    NetworkServer &operator=(const NetworkServer &) = delete;

    void begin(uint16_t port = 0);
    void begin(uint16_t port, int reuse_enable);
    /* The next waiting connection, or a client that is not connected. */
    NetworkClient accept();
    NetworkClient available() __attribute__((deprecated("Renamed to accept()."))) { return accept(); }
    bool hasClient();
    void setNoDelay(bool nodelay) { _noDelay = nodelay; }
    bool getNoDelay() { return _noDelay; }
    void end();
    void close() { end(); }
    void stop() { end(); }
    operator bool() { return _listen != nullptr; }

private:
    static int8_t onAccept(void *arg, struct tcp_pcb *pcb, int8_t err);
    struct tcp_pcb *_listen = nullptr;
    struct NetworkTcpConnection **_pending = nullptr;   /* max_clients slots, oldest first */
    uint32_t _addr = 0;
    uint16_t _port;
    uint8_t _max;
    bool _noDelay = false;
};
