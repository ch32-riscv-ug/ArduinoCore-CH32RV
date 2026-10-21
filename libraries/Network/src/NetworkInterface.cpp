#include "NetworkInterface.h"
#include "NetworkCore.h"
#include "NetworkManager.h"

#include <string.h>

#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/etharp.h"
#include "lwip/igmp.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "netif/ethernet.h"

/* The interface behind a netif: netif_add() keeps it as the netif's state. */
static NetworkInterface *owner(struct netif *n)
{
    return static_cast<NetworkInterface *>(n->state);
}

NetworkInterface::NetworkInterface() {}

NetworkInterface::~NetworkInterface()
{
    netifStop();
}

bool NetworkInterface::config(IPAddress local, IPAddress gateway, IPAddress subnet, IPAddress dns1, IPAddress dns2,
                              IPAddress dns3)
{
    (void)dns3;   /* lwIP keeps two DNS servers */
    _ip = (uint32_t)local;
    _gateway = (uint32_t)gateway;
    _mask = (uint32_t)subnet;
    if (_ip && !_mask) {
        _mask = PP_HTONL(0xFFFFFF00UL);   /* a /24, as ESP32 assumes too */
    }
    _dns[0] = (uint32_t)dns1;
    _dns[1] = (uint32_t)dns2;
    if (_netif) {
        applyAddress();
    }
    return true;
}

bool NetworkInterface::dnsIP(uint8_t dns_no, IPAddress ip)
{
    if (dns_no >= 2) {
        return false;
    }
    _dns[dns_no] = (uint32_t)ip;
    if (_dns[dns_no]) {
        ch32rv_net::start();
        ip_addr_t a;
        ip_addr_set_ip4_u32(&a, _dns[dns_no]);
        dns_setserver(dns_no, &a);
    }
    return true;
}

const char *NetworkInterface::getHostname() const
{
    return _hostname;
}

bool NetworkInterface::setHostname(const char *hostname)
{
    if (!hostname || strlen(hostname) >= sizeof _hostname) {
        return false;
    }
    strcpy(_hostname, hostname);
    return true;
}

bool NetworkInterface::started() const
{
    return _netif != nullptr;
}

bool NetworkInterface::linkUp() const
{
    return _netif && _link;
}

bool NetworkInterface::connected() const
{
    return linkUp();
}

bool NetworkInterface::hasIP() const
{
    return _netif && _link && !ip4_addr_isany_val(*netif_ip4_addr(_netif));
}

bool NetworkInterface::setDefault()
{
    if (!_netif) {
        return false;
    }
    netif_set_default(_netif);
    return true;
}

bool NetworkInterface::isDefault() const
{
    return _netif && netif_default == _netif;
}

uint8_t *NetworkInterface::macAddress(uint8_t *mac) const
{
    memcpy(mac, _mac, 6);
    return mac;
}

String NetworkInterface::macAddress() const
{
    char text[18];
    snprintf(text, sizeof text, "%02X:%02X:%02X:%02X:%02X:%02X", _mac[0], _mac[1], _mac[2], _mac[3], _mac[4], _mac[5]);
    return String(text);
}

IPAddress NetworkInterface::localIP() const
{
    return _netif ? IPAddress(ip4_addr_get_u32(netif_ip4_addr(_netif))) : IPAddress((uint32_t)0);
}

IPAddress NetworkInterface::subnetMask() const
{
    return _netif ? IPAddress(ip4_addr_get_u32(netif_ip4_netmask(_netif))) : IPAddress((uint32_t)0);
}

IPAddress NetworkInterface::gatewayIP() const
{
    return _netif ? IPAddress(ip4_addr_get_u32(netif_ip4_gw(_netif))) : IPAddress((uint32_t)0);
}

IPAddress NetworkInterface::dnsIP(uint8_t dns_no) const
{
    if (dns_no >= 2) {
        return IPAddress((uint32_t)0);
    }
    ch32rv_net::start();
    const ip_addr_t *a = dns_getserver(dns_no);
    return IPAddress(ip_addr_get_ip4_u32(a));
}

IPAddress NetworkInterface::broadcastIP() const
{
    const uint32_t ip = (uint32_t)localIP(), mask = (uint32_t)subnetMask();
    return IPAddress(ip | ~mask);
}

IPAddress NetworkInterface::networkID() const
{
    return IPAddress((uint32_t)localIP() & (uint32_t)subnetMask());
}

uint8_t NetworkInterface::subnetCIDR() const
{
    uint32_t mask = lwip_ntohl((uint32_t)subnetMask());
    uint8_t bits = 0;
    while (mask & 0x80000000u) {
        bits++;
        mask <<= 1;
    }
    return bits;
}

struct netif *NetworkInterface::netif() const
{
    return _netif;
}

size_t NetworkInterface::printDriverInfo(Print &out) const
{
    (void)out;
    return 0;
}

size_t NetworkInterface::printTo(Print &out) const
{
    size_t n = 0;
    n += out.print(_netif ? (char)_netif->name[0] : '-');
    n += out.print(_netif ? (char)_netif->name[1] : '-');
    n += out.print(": <");
    n += out.print(linkUp() ? "UP" : "DOWN");
    if (isDefault()) {
        n += out.print(",DEFAULT");
    }
    n += out.print(_ip ? ",STATIC" : ",DHCP");
    n += out.print("> ");
    n += printDriverInfo(out);
    n += out.print(" ether ");
    n += out.print(macAddress());
    n += out.print(" inet ");
    n += out.print(localIP());
    n += out.print('/');
    n += out.print(subnetCIDR());
    n += out.print(" gateway ");
    n += out.print(gatewayIP());
    n += out.print(" dns ");
    n += out.print(dnsIP(0));
    return n;
}

/* lwIP hands us a frame to send. Usually one pbuf (the stack builds TCP and
 * UDP in place); a chain is gathered into one buffer first. */
int8_t NetworkInterface::output(struct netif *n, struct pbuf *p)
{
    NetworkInterface *self = owner(n);
    if (!self || !self->_link) {
        return ERR_IF;
    }
    if (!p->next) {
        return self->transmitFrame(static_cast<const uint8_t *>(p->payload), p->len) ? ERR_OK : ERR_MEM;
    }
    static uint8_t gather[1536];
    if (p->tot_len > sizeof gather) {
        return ERR_BUF;
    }
    const uint16_t length = pbuf_copy_partial(p, gather, sizeof gather, 0);
    return self->transmitFrame(gather, length) ? ERR_OK : ERR_MEM;
}

int8_t NetworkInterface::init(struct netif *n)
{
    NetworkInterface *self = owner(n);
    n->name[0] = 'e';
    n->name[1] = 'n';
    n->output = etharp_output;
    n->linkoutput = output;
    n->hwaddr_len = 6;
    memcpy(n->hwaddr, self->_mac, 6);
    n->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_IGMP;
    netif_set_hostname(n, self->_hostname);
    return ERR_OK;
}

/* Address changes (DHCP bound, renewed, lost). The DNS servers set by hand
 * override the ones DHCP brought. */
void NetworkInterface::statusChanged(struct netif *n)
{
    NetworkInterface *self = owner(n);
    if (!self) {
        return;
    }
    for (uint8_t i = 0; i < 2; i++) {
        if (self->_dns[i]) {
            ip_addr_t a;
            ip_addr_set_ip4_u32(&a, self->_dns[i]);
            dns_setserver(i, &a);
        }
    }
}

/* Static when config() gave an address, DHCP otherwise. */
void NetworkInterface::applyAddress()
{
    if (_ip) {
        dhcp_release_and_stop(_netif);
        ip4_addr_t ip, mask, gw;
        ip4_addr_set_u32(&ip, _ip);
        ip4_addr_set_u32(&mask, _mask);
        ip4_addr_set_u32(&gw, _gateway);
        netif_set_addr(_netif, &ip, &mask, &gw);
    } else {
        netif_set_addr(_netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4);
        if (_link) {
            dhcp_start(_netif);
        }
    }
    statusChanged(_netif);
}

bool NetworkInterface::netifStart(const uint8_t mac[6], uint16_t mtu)
{
    ch32rv_net::start();
    if (_netif) {
        return true;
    }
    memcpy(_mac, mac, 6);
    if (!_hostname[0] && NetworkManager::getHostname()[0]) {
        setHostname(NetworkManager::getHostname());
    }
    if (!_hostname[0]) {
        snprintf(_hostname, sizeof _hostname, "ch32rv-%02x%02x%02x", mac[3], mac[4], mac[5]);
    }
    _netif = new struct netif;
    if (!_netif) {
        return false;
    }
    memset(_netif, 0, sizeof *_netif);
    if (!netif_add(_netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4, this, init, ethernet_input)) {
        delete _netif;
        _netif = nullptr;
        return false;
    }
    _netif->mtu = mtu;
    netif_set_status_callback(_netif, statusChanged);
    if (!netif_default) {
        netif_set_default(_netif);
    }
    netif_set_up(_netif);
    ch32rv_net::addInterface(this);
    applyAddress();
    if (_link) {
        netif_set_link_up(_netif);
    }
    return true;
}

void NetworkInterface::netifStop()
{
    if (!_netif) {
        return;
    }
    ch32rv_net::removeInterface(this);
    dhcp_release_and_stop(_netif);
    dhcp_cleanup(_netif);
    netif_remove(_netif);
    delete _netif;
    _netif = nullptr;
}

void NetworkInterface::frameReceived(const uint8_t *frame, size_t length)
{
    if (!_netif || length > 0xFFFF) {
        return;
    }
    struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)length, PBUF_POOL);
    if (!p) {
        return;   /* out of receive buffers: dropped, as a NIC would */
    }
    pbuf_take(p, frame, (u16_t)length);
    if (_netif->input(p, _netif) != ERR_OK) {
        pbuf_free(p);
    }
}

void NetworkInterface::setLinkUp(bool up)
{
    if (_link == up) {
        return;
    }
    _link = up;
    if (!_netif) {
        return;
    }
    if (up) {
        netif_set_link_up(_netif);
        if (!_ip) {
            dhcp_start(_netif);
        }
    } else {
        netif_set_link_down(_netif);
        if (!_ip) {
            dhcp_release_and_stop(_netif);
            netif_set_addr(_netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4);
        }
    }
}
