#include "NetworkUdp.h"
#include "NetworkCore.h"
#include "NetworkManager.h"

#include <string.h>

#include "lwip/igmp.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

static const uint16_t UDP_MAX_PAYLOAD = 1472;

struct NetworkUdpGlue {
    static void onRecv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
    {
        (void)pcb;
        auto *self = static_cast<NetworkUDP *>(arg);
        if (self->_queued >= sizeof self->_queue / sizeof self->_queue[0]) {
            pbuf_free(p);   /* the sketch is not keeping up: dropped */
            return;
        }
        self->_queue[self->_queued++] = {p, ip_addr_get_ip4_u32(addr), port};
    }
};

NetworkUDP::NetworkUDP() {}

NetworkUDP::~NetworkUDP()
{
    stop();
}

uint8_t NetworkUDP::begin(IPAddress a, uint16_t p)
{
    ch32rv_net::start();
    stop();
    _pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    if (!_pcb) {
        return 0;
    }
    ip_set_option(_pcb, SOF_BROADCAST);
    ip_addr_t addr;
    ip_addr_set_ip4_u32(&addr, (uint32_t)a);
    if (udp_bind(_pcb, &addr, p) != ERR_OK) {
        udp_remove(_pcb);
        _pcb = nullptr;
        return 0;
    }
    _port = p;
    udp_recv(_pcb, NetworkUdpGlue::onRecv, this);
    return 1;
}

uint8_t NetworkUDP::begin(uint16_t p)
{
    return begin(IPAddress((uint32_t)0), p);
}

uint8_t NetworkUDP::beginMulticast(IPAddress a, uint16_t p)
{
    if (!begin(IPAddress((uint32_t)0), p)) {
        return 0;
    }
    ip4_addr_t group;
    ip4_addr_set_u32(&group, (uint32_t)a);
    if (igmp_joingroup(IP4_ADDR_ANY4, &group) != ERR_OK) {
        stop();
        return 0;
    }
    _multicast = (uint32_t)a;
    return 1;
}

void NetworkUDP::stop()
{
    if (_multicast) {
        ip4_addr_t group;
        ip4_addr_set_u32(&group, _multicast);
        igmp_leavegroup(IP4_ADDR_ANY4, &group);
        _multicast = 0;
    }
    if (_pcb) {
        udp_remove(_pcb);
        _pcb = nullptr;
    }
    clear();
    while (_queued) {
        pbuf_free(_queue[--_queued].p);
    }
    free(_tx);
    _tx = nullptr;
    _txLength = 0;
}

int NetworkUDP::beginMulticastPacket()
{
    return _multicast && _port ? beginPacket(IPAddress(_multicast), _port) : 0;
}

int NetworkUDP::beginPacket()
{
    return _current.p ? beginPacket(IPAddress(_current.ip), _current.port) : 0;
}

int NetworkUDP::beginPacket(IPAddress ip, uint16_t port)
{
    ch32rv_net::start();
    if (!_tx) {
        _tx = static_cast<uint8_t *>(malloc(UDP_MAX_PAYLOAD));
        if (!_tx) {
            return 0;
        }
    }
    _txLength = 0;
    _txIp = (uint32_t)ip;
    _txPort = port;
    return 1;
}

int NetworkUDP::beginPacket(const char *host, uint16_t port)
{
    IPAddress ip;
    return Network.hostByName(host, ip) ? beginPacket(ip, port) : 0;
}

size_t NetworkUDP::write(uint8_t b)
{
    return write(&b, 1);
}

size_t NetworkUDP::write(const uint8_t *buffer, size_t size)
{
    if (!_tx) {
        return 0;
    }
    if (size > (size_t)(UDP_MAX_PAYLOAD - _txLength)) {
        size = UDP_MAX_PAYLOAD - _txLength;
    }
    memcpy(_tx + _txLength, buffer, size);
    _txLength = (uint16_t)(_txLength + size);
    return size;
}

int NetworkUDP::endPacket()
{
    if (!_tx || !_txPort) {
        return 0;
    }
    if (!_pcb && !begin(0)) {   /* sending without begin(): an ephemeral port */
        return 0;
    }
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, _txLength, PBUF_RAM);
    if (!p) {
        return 0;
    }
    memcpy(p->payload, _tx, _txLength);
    ip_addr_t to;
    ip_addr_set_ip4_u32(&to, _txIp);
    const err_t err = udp_sendto(_pcb, p, &to, _txPort);
    pbuf_free(p);
    _txLength = 0;
    _txPort = 0;
    return err == ERR_OK;
}

int NetworkUDP::parsePacket()
{
    clear();
    if (!_queued) {
        ch32rv_net::pump();
    }
    if (!_queued) {
        return 0;
    }
    _current = _queue[0];
    for (uint8_t i = 1; i < _queued; i++) {
        _queue[i - 1] = _queue[i];
    }
    _queued--;
    _offset = 0;
    return _current.p->tot_len;
}

int NetworkUDP::available()
{
    return _current.p ? _current.p->tot_len - _offset : 0;
}

int NetworkUDP::read()
{
    unsigned char b;
    return read(&b, 1) == 1 ? b : -1;
}

int NetworkUDP::read(unsigned char *buffer, size_t len)
{
    if (!_current.p) {
        return -1;
    }
    const uint16_t n = pbuf_copy_partial(_current.p, buffer, (u16_t)(len > 0xFFFF ? 0xFFFF : len), _offset);
    _offset = (uint16_t)(_offset + n);
    return n;
}

int NetworkUDP::peek()
{
    if (!available()) {
        return -1;
    }
    return pbuf_get_at(_current.p, _offset);
}

void NetworkUDP::clear()
{
    if (_current.p) {
        pbuf_free(_current.p);
    }
    _current = {};
    _offset = 0;
}

IPAddress NetworkUDP::remoteIP()
{
    return IPAddress(_current.ip);
}

uint16_t NetworkUDP::remotePort()
{
    return _current.port;
}
