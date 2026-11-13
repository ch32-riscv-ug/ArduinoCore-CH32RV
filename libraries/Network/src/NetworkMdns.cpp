#include "NetworkMdns.h"
#include "internal/NetworkCore.h"

#include <string.h>

#include "lwip/apps/mdns.h"
#include "lwip/apps/mdns_out.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"

NetworkMdns MDNS;

namespace {
struct Service {
    char service[17];      /* "_http" */
    uint8_t proto;         /* DNSSD_PROTO_TCP / UDP */
    uint16_t port;
    uint8_t txtLength;
    uint8_t txt[96];       /* items, each after a length byte */
    bool used;
};

bool running;
char host[MDNS_LABEL_MAXLEN + 1];
char instance[MDNS_LABEL_MAXLEN + 1];
Service services[MDNS_MAX_SERVICES];
}

static void txt_items(struct mdns_service *service, void *userdata)
{
    const Service *s = static_cast<const Service *>(userdata);
    for (uint8_t at = 0; at < s->txtLength; at = (uint8_t)(at + 1 + s->txt[at])) {
        mdns_resp_add_service_txtitem(service, reinterpret_cast<const char *>(s->txt + at + 1), s->txt[at]);
    }
}

static void add_services(struct netif *n)
{
    for (auto &s : services) {
        if (s.used) {
            mdns_resp_add_service(n, instance, s.service, (enum mdns_sd_proto)s.proto, s.port, txt_items, &s);
        }
    }
}

/* mdns_resp_remove_netif() stops the probe timer only: the delayed answers and the multicast holds it set with
 * mdns_set_timeout() (MDNS_RESPONSE_DELAY, 20 to 120 ms after a question) stay queued with the netif as their
 * argument, and run on it after the interface is gone - a netif deleted by NetworkInterface::netifStop() (a driver's
 * end()) was read again (heap-use-after-free in netif_mdns_data). Cancel them first. */
static void remove_netif(struct netif *n)
{
    sys_untimeout(mdns_send_multicast_msg_delayed_ipv4, n);
    sys_untimeout(mdns_send_unicast_msg_delayed_ipv4, n);
    sys_untimeout(mdns_multicast_timeout_reset_ipv4, n);
    sys_untimeout(mdns_multicast_probe_timeout_reset_ipv4, n);
    sys_untimeout(mdns_multicast_timeout_25ttl_reset_ipv4, n);
#if LWIP_IPV6
    sys_untimeout(mdns_send_multicast_msg_delayed_ipv6, n);
    sys_untimeout(mdns_send_unicast_msg_delayed_ipv6, n);
    sys_untimeout(mdns_multicast_timeout_reset_ipv6, n);
    sys_untimeout(mdns_multicast_probe_timeout_reset_ipv6, n);
    sys_untimeout(mdns_multicast_timeout_25ttl_reset_ipv6, n);
#endif
    mdns_resp_remove_netif(n);
}

/* Interfaces starting and stopping, from the Network library. */
static void netif_changed(struct netif *n, bool up)
{
    if (!n->state) {
        return;   /* the loopback interface: no NetworkInterface, nothing to announce */
    }
    if (up) {
        if (mdns_resp_add_netif(n, host) == ERR_OK) {
            add_services(n);
            mdns_resp_announce(n);
        }
    } else if (mdns_resp_netif_active(n)) {
        remove_netif(n);
    }
}

bool NetworkMdns::begin(const char *hostName)
{
    if (!hostName || !*hostName || strlen(hostName) > MDNS_LABEL_MAXLEN) {
        return false;
    }
    ch32rv_net::start();
    end();
    strcpy(host, hostName);
    if (!instance[0]) {
        strcpy(instance, hostName);
    }
    static bool initialised;
    if (!initialised) {
        initialised = true;
        mdns_resp_init();
    }
    running = true;
    ch32rv_net::setNetifListener(netif_changed);
    struct netif *n;
    NETIF_FOREACH(n) {
        netif_changed(n, true);
    }
    return true;
}

void NetworkMdns::end()
{
    if (!running) {
        return;
    }
    running = false;
    ch32rv_net::setNetifListener(nullptr);
    struct netif *n;
    NETIF_FOREACH(n) {
        if (mdns_resp_netif_active(n)) {
            remove_netif(n);
        }
    }
}

bool NetworkMdns::setInstanceName(const char *name)
{
    if (!name || strlen(name) > MDNS_LABEL_MAXLEN) {
        return false;
    }
    strcpy(instance, name);
    return true;
}

/* "http" or "_http" -> "_http". */
static bool label(char *out, size_t size, const char *in)
{
    if (!in || !*in) {
        return false;
    }
    const char *bare = in[0] == '_' ? in + 1 : in;
    if (strlen(bare) + 2 > size) {
        return false;
    }
    out[0] = '_';
    strcpy(out + 1, bare);
    return true;
}

static Service *find(const char *service, const char *proto)
{
    char name[sizeof services[0].service];
    if (!label(name, sizeof name, service)) {
        return nullptr;
    }
    const uint8_t p = (proto && strstr(proto, "udp")) ? DNSSD_PROTO_UDP : DNSSD_PROTO_TCP;
    for (auto &s : services) {
        if (s.used && s.proto == p && !strcmp(s.service, name)) {
            return &s;
        }
    }
    return nullptr;
}

bool NetworkMdns::addService(const char *service, const char *proto, uint16_t port)
{
    if (find(service, proto)) {
        return true;
    }
    for (auto &s : services) {
        if (!s.used) {
            if (!label(s.service, sizeof s.service, service)) {
                return false;
            }
            s.proto = (proto && strstr(proto, "udp")) ? DNSSD_PROTO_UDP : DNSSD_PROTO_TCP;
            s.port = port;
            s.txtLength = 0;
            s.used = true;
            if (running) {
                struct netif *n;
                NETIF_FOREACH(n) {
                    if (mdns_resp_netif_active(n)) {
                        mdns_resp_add_service(n, instance, s.service, (enum mdns_sd_proto)s.proto, s.port, txt_items, &s);
                        mdns_resp_announce(n);
                    }
                }
            }
            return true;
        }
    }
    return false;
}

bool NetworkMdns::addServiceTxt(const char *service, const char *proto, const char *key, const char *value)
{
    Service *s = find(service, proto);
    if (!s || !key) {
        return false;
    }
    const size_t k = strlen(key), v = value ? strlen(value) : 0;
    const size_t item = k + (value ? 1 + v : 0);
    if (item > 63 || s->txtLength + 1 + item > sizeof s->txt) {
        return false;
    }
    uint8_t *at = s->txt + s->txtLength;
    *at++ = (uint8_t)item;
    memcpy(at, key, k);
    if (value) {
        at[k] = '=';
        memcpy(at + k + 1, value, v);
    }
    s->txtLength = (uint8_t)(s->txtLength + 1 + item);
    if (running) {
        struct netif *n;
        NETIF_FOREACH(n) {
            if (mdns_resp_netif_active(n)) {
                mdns_resp_announce(n);
            }
        }
    }
    return true;
}
