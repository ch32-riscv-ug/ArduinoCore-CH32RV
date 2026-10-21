/* A TCP connection, as ESP32's NetworkClient. Copies share the connection; it
 * closes when stop() is called or the last copy goes away.
 *
 * Received data is copied into a buffer of the client's own (TCP_WND bytes,
 * allocated while connected), and the window opens as the sketch reads, so a
 * slow reader slows the sender instead of losing data. write() waits for room
 * in the stack's send buffer, running the stack meanwhile, and gives up after
 * 10 s without progress. available(), read(), peek() and connected() run the
 * stack once when nothing has arrived, so a sketch (or Stream's timed reads)
 * polling them in a loop keeps receiving. */
#pragma once
#include <Arduino.h>
#include <api/Client.h>

struct NetworkTcpConnection;

class NetworkClient : public Client {
public:
    NetworkClient();
    NetworkClient(const NetworkClient &other);
    NetworkClient &operator=(const NetworkClient &other);
    ~NetworkClient();

    int connect(IPAddress ip, uint16_t port) override;
    int connect(IPAddress ip, uint16_t port, int32_t timeout_ms);
    int connect(const char *host, uint16_t port) override;
    int connect(const char *host, uint16_t port, int32_t timeout_ms);
    size_t write(uint8_t data) override;
    size_t write(const uint8_t *buf, size_t size) override;
    using Print::write;
    int available() override;
    int read() override;
    int read(uint8_t *buf, size_t size) override;
    int peek() override;
    void flush() override;      /* pushes out what write() queued */
    void clear();               /* drops what was received and not read */
    void stop() override;
    uint8_t connected() override;
    operator bool() override { return connected(); }
    bool operator==(const NetworkClient &other) const { return _conn == other._conn; }
    bool operator!=(const NetworkClient &other) const { return _conn != other._conn; }

    void setConnectionTimeout(uint32_t milliseconds) { _connectTimeout = milliseconds; }
    int setNoDelay(bool nodelay);
    bool getNoDelay();
    IPAddress remoteIP() const;
    uint16_t remotePort() const;
    IPAddress localIP() const;
    uint16_t localPort() const;

    /* For NetworkServer. */
    explicit NetworkClient(NetworkTcpConnection *conn);

private:
    void release();
    NetworkTcpConnection *_conn = nullptr;
    uint32_t _connectTimeout = 3000;
};
