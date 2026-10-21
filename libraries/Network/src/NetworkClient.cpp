#include "NetworkClient.h"
#include "NetworkCore.h"
#include "NetworkManager.h"
#include "NetworkTcp.h"

#include <string.h>

/* ---- the connection: lwIP's callbacks */

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    auto *c = static_cast<NetworkTcpConnection *>(arg);
    (void)err;
    if (!c) {
        if (p) {
            tcp_recved(pcb, p->tot_len);
            pbuf_free(p);
        }
        return ERR_OK;
    }
    if (!p) {
        c->peerClosed = true;
        return ERR_OK;
    }
    if (!c->rx) {
        c->rx = static_cast<uint8_t *>(malloc(TCP_WND));
        if (!c->rx) {
            return ERR_MEM;   /* lwIP keeps the data and offers it again */
        }
    }
    /* The window never exceeds the free space, so this fits; if it does not,
     * lwIP holds the pbuf (refused data) and offers it again later. */
    if (p->tot_len > TCP_WND - c->rxCount) {
        return ERR_MEM;
    }
    uint16_t tail = (uint16_t)((c->rxHead + c->rxCount) % TCP_WND);
    for (struct pbuf *q = p; q; q = q->next) {
        const uint8_t *src = static_cast<const uint8_t *>(q->payload);
        uint16_t left = q->len;
        while (left) {
            uint16_t n = (uint16_t)(TCP_WND - tail);
            if (n > left) {
                n = left;
            }
            memcpy(c->rx + tail, src, n);
            src += n;
            left = (uint16_t)(left - n);
            tail = (uint16_t)((tail + n) % TCP_WND);
        }
    }
    c->rxCount = (uint16_t)(c->rxCount + p->tot_len);
    pbuf_free(p);   /* the window opens again only as the sketch reads */
    return ERR_OK;
}

static void on_err(void *arg, err_t err)
{
    auto *c = static_cast<NetworkTcpConnection *>(arg);
    if (c) {
        c->pcb = nullptr;   /* lwIP has freed it */
        c->error = err;
        c->connecting = false;
        c->peerClosed = true;
    }
}

static err_t on_connected(void *arg, struct tcp_pcb *pcb, err_t err)
{
    (void)pcb;
    auto *c = static_cast<NetworkTcpConnection *>(arg);
    if (c) {
        c->connecting = false;
        c->established = err == ERR_OK;
        c->error = err;
    }
    return ERR_OK;
}

NetworkTcpConnection *NetworkTcpConnection::attach(struct tcp_pcb *pcb)
{
    auto *c = new NetworkTcpConnection;
    if (!c) {
        return nullptr;
    }
    c->pcb = pcb;
    tcp_arg(pcb, c);
    tcp_recv(pcb, on_recv);
    tcp_err(pcb, on_err);
    return c;
}

void NetworkTcpConnection::close()
{
    if (!pcb) {
        return;
    }
    tcp_arg(pcb, nullptr);
    tcp_recv(pcb, nullptr);
    tcp_err(pcb, nullptr);
    tcp_sent(pcb, nullptr);
    tcp_poll(pcb, nullptr, 0);
    if (tcp_close(pcb) != ERR_OK) {
        tcp_abort(pcb);
    }
    pcb = nullptr;
    established = false;
}

void NetworkTcpConnection::release()
{
    if (--refs) {
        return;
    }
    close();
    free(rx);
    delete this;
}

/* ---- NetworkClient */

static const uint32_t WRITE_STALL_MS = 10000;

NetworkClient::NetworkClient() {}

NetworkClient::NetworkClient(NetworkTcpConnection *conn) : _conn(conn) {}

NetworkClient::NetworkClient(const NetworkClient &other) : Client(), _conn(other._conn), _connectTimeout(other._connectTimeout)
{
    if (_conn) {
        _conn->retain();
    }
    _timeout = other._timeout;
}

NetworkClient &NetworkClient::operator=(const NetworkClient &other)
{
    if (this != &other) {
        if (other._conn) {
            other._conn->retain();
        }
        release();
        _conn = other._conn;
        _connectTimeout = other._connectTimeout;
        _timeout = other._timeout;
    }
    return *this;
}

NetworkClient::~NetworkClient()
{
    release();
}

void NetworkClient::release()
{
    if (_conn) {
        _conn->release();
        _conn = nullptr;
    }
}

int NetworkClient::connect(IPAddress ip, uint16_t port)
{
    return connect(ip, port, (int32_t)_connectTimeout);
}

int NetworkClient::connect(IPAddress ip, uint16_t port, int32_t timeout_ms)
{
    ch32rv_net::start();
    stop();
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) {
        return 0;
    }
    NetworkTcpConnection *c = NetworkTcpConnection::attach(pcb);
    if (!c) {
        tcp_abort(pcb);
        return 0;
    }
    _conn = c;
    c->connecting = true;
    ip_addr_t addr;
    ip_addr_set_ip4_u32(&addr, (uint32_t)ip);
    if (tcp_connect(pcb, &addr, port, on_connected) != ERR_OK) {
        c->connecting = false;
        stop();
        return 0;
    }
    ch32rv_net::waitFor([](void *a) { return !static_cast<NetworkTcpConnection *>(a)->connecting; }, c,
                        timeout_ms > 0 ? (uint32_t)timeout_ms : 0);
    if (!c->established) {
        stop();
        return 0;
    }
    return 1;
}

int NetworkClient::connect(const char *host, uint16_t port)
{
    return connect(host, port, (int32_t)_connectTimeout);
}

int NetworkClient::connect(const char *host, uint16_t port, int32_t timeout_ms)
{
    IPAddress ip;
    if (!Network.hostByName(host, ip)) {
        return 0;
    }
    return connect(ip, port, timeout_ms);
}

size_t NetworkClient::write(uint8_t data)
{
    return write(&data, 1);
}

size_t NetworkClient::write(const uint8_t *buf, size_t size)
{
    size_t done = 0;
    uint32_t since = millis();
    while (done < size && _conn && _conn->pcb && _conn->established) {
        struct tcp_pcb *pcb = _conn->pcb;
        size_t n = tcp_sndbuf(pcb);
        if (n > size - done) {
            n = size - done;
        }
        if (n && tcp_sndqueuelen(pcb) < TCP_SND_QUEUELEN) {
            const u8_t flags = TCP_WRITE_FLAG_COPY | (done + n < size ? TCP_WRITE_FLAG_MORE : 0);
            if (tcp_write(pcb, buf + done, (u16_t)n, flags) == ERR_OK) {
                done += n;
                since = millis();
                continue;
            }
        }
        /* No room: send what is queued and let the ACKs come in. A lost
         * segment waits for TCP's retransmission timer (a second or more,
         * doubling), so this gives up only after 10 s without progress - as
         * ESP32 does (ten tries of one second). */
        tcp_output(pcb);
        if ((uint32_t)(millis() - since) >= WRITE_STALL_MS) {
            break;
        }
        ch32rv_net::pump();
    }
    if (_conn && _conn->pcb) {
        tcp_output(_conn->pcb);
    }
    return done;
}

int NetworkClient::available()
{
    if (!_conn) {
        return 0;
    }
    if (!_conn->rxCount) {
        ch32rv_net::pump();   /* a sketch polling available() in a loop keeps the stack running */
    }
    return _conn ? _conn->rxCount : 0;
}

int NetworkClient::read()
{
    uint8_t b;
    return read(&b, 1) == 1 ? b : -1;
}

int NetworkClient::read(uint8_t *buf, size_t size)
{
    if (!_conn || !_conn->rxCount) {
        return _conn && !_conn->peerClosed ? 0 : -1;
    }
    NetworkTcpConnection *c = _conn;
    size_t n = c->rxCount < size ? c->rxCount : size;
    for (size_t i = 0; i < n; i++) {
        buf[i] = c->rx[(c->rxHead + i) % TCP_WND];
    }
    c->rxHead = (uint16_t)((c->rxHead + n) % TCP_WND);
    c->rxCount = (uint16_t)(c->rxCount - n);
    if (c->pcb) {
        tcp_recved(c->pcb, (u16_t)n);
    }
    return (int)n;
}

int NetworkClient::peek()
{
    if (!_conn || !_conn->rxCount) {
        return -1;
    }
    return _conn->rx[_conn->rxHead];
}

void NetworkClient::flush()
{
    if (_conn && _conn->pcb) {
        tcp_output(_conn->pcb);
    }
}

void NetworkClient::clear()
{
    if (!_conn) {
        return;
    }
    const uint16_t n = _conn->rxCount;
    _conn->rxHead = 0;
    _conn->rxCount = 0;
    if (_conn->pcb && n) {
        tcp_recved(_conn->pcb, n);
    }
}

void NetworkClient::stop()
{
    if (_conn) {
        _conn->close();
    }
    release();
}

uint8_t NetworkClient::connected()
{
    if (!_conn) {
        return 0;
    }
    if (!_conn->rxCount) {
        ch32rv_net::pump();
    }
    /* Checked after the pump: the last data and the peer's FIN can arrive in
     * the same round, and data still to read counts as connected. */
    if (_conn->rxCount) {
        return 1;
    }
    return _conn->pcb && _conn->established && !_conn->peerClosed;
}

int NetworkClient::setNoDelay(bool nodelay)
{
    if (!_conn || !_conn->pcb) {
        return -1;
    }
    if (nodelay) {
        tcp_nagle_disable(_conn->pcb);
    } else {
        tcp_nagle_enable(_conn->pcb);
    }
    return 0;
}

bool NetworkClient::getNoDelay()
{
    return _conn && _conn->pcb && tcp_nagle_disabled(_conn->pcb);
}

IPAddress NetworkClient::remoteIP() const
{
    return _conn && _conn->pcb ? IPAddress(ip_addr_get_ip4_u32(&_conn->pcb->remote_ip)) : IPAddress((uint32_t)0);
}

uint16_t NetworkClient::remotePort() const
{
    return _conn && _conn->pcb ? _conn->pcb->remote_port : 0;
}

IPAddress NetworkClient::localIP() const
{
    return _conn && _conn->pcb ? IPAddress(ip_addr_get_ip4_u32(&_conn->pcb->local_ip)) : IPAddress((uint32_t)0);
}

uint16_t NetworkClient::localPort() const
{
    return _conn && _conn->pcb ? _conn->pcb->local_port : 0;
}
