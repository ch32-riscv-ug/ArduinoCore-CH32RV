#include "NetworkServer.h"
#include "NetworkCore.h"
#include "NetworkTcp.h"

NetworkServer::NetworkServer(uint16_t port, uint8_t max_clients) : _port(port), _max(max_clients ? max_clients : 1) {}

NetworkServer::NetworkServer(const IPAddress &addr, uint16_t port, uint8_t max_clients)
    : _addr((uint32_t)addr), _port(port), _max(max_clients ? max_clients : 1)
{
}

NetworkServer::~NetworkServer()
{
    end();
}

void NetworkServer::begin(uint16_t port)
{
    begin(port, 1);
}

void NetworkServer::begin(uint16_t port, int reuse_enable)
{
    ch32rv_net::start();
    if (_listen) {
        return;
    }
    if (port) {
        _port = port;
    }
    if (!_pending) {
        _pending = new NetworkTcpConnection *[_max]();
        if (!_pending) {
            return;
        }
    }
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) {
        return;
    }
    if (reuse_enable) {
        ip_set_option(pcb, SOF_REUSEADDR);
    }
    ip_addr_t addr;
    ip_addr_set_ip4_u32(&addr, _addr);
    if (tcp_bind(pcb, &addr, _port) != ERR_OK) {
        tcp_abort(pcb);
        return;
    }
    struct tcp_pcb *listening = tcp_listen_with_backlog(pcb, _max);
    if (!listening) {
        tcp_abort(pcb);
        return;
    }
    _listen = listening;
    tcp_arg(_listen, this);
    tcp_accept(_listen, onAccept);
}

int8_t NetworkServer::onAccept(void *arg, struct tcp_pcb *pcb, int8_t err)
{
    auto *self = static_cast<NetworkServer *>(arg);
    if (err != ERR_OK || !pcb) {
        return ERR_VAL;
    }
    uint8_t free_slot = self->_max;
    for (uint8_t i = 0; i < self->_max; i++) {
        if (!self->_pending[i]) {
            free_slot = i;
            break;
        }
    }
    NetworkTcpConnection *c = free_slot < self->_max ? NetworkTcpConnection::attach(pcb) : nullptr;
    if (!c) {
        tcp_abort(pcb);   /* full: refused with a reset */
        return ERR_ABRT;
    }
    c->established = true;
    if (self->_noDelay) {
        tcp_nagle_disable(pcb);
    }
    self->_pending[free_slot] = c;
    return ERR_OK;
}

bool NetworkServer::hasClient()
{
    if (!_listen) {
        return false;
    }
    ch32rv_net::pump();
    for (uint8_t i = 0; i < _max; i++) {
        if (_pending[i]) {
            return true;
        }
    }
    return false;
}

NetworkClient NetworkServer::accept()
{
    if (!_listen) {
        return NetworkClient();
    }
    ch32rv_net::pump();
    /* Oldest first: the slots fill in order, and accept() shifts them down. */
    NetworkTcpConnection *c = _pending[0];
    if (!c) {
        for (uint8_t i = 1; i < _max && !c; i++) {
            if (_pending[i]) {
                c = _pending[i];
                _pending[i] = nullptr;
            }
        }
        return NetworkClient(c);
    }
    for (uint8_t i = 1; i < _max; i++) {
        _pending[i - 1] = _pending[i];
    }
    _pending[_max - 1] = nullptr;
    return NetworkClient(c);
}

void NetworkServer::end()
{
    if (_listen) {
        tcp_arg(_listen, nullptr);
        tcp_accept(_listen, nullptr);
        tcp_close(_listen);
        _listen = nullptr;
    }
    if (_pending) {
        for (uint8_t i = 0; i < _max; i++) {
            if (_pending[i]) {
                _pending[i]->release();
                _pending[i] = nullptr;
            }
        }
    }
}
