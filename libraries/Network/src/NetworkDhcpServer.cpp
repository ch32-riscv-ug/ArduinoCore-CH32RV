/* A small DHCP server (RFC 2131) for one interface: DISCOVER / OFFER,
 * REQUEST / ACK or NAK, RELEASE, DECLINE. No relays, no INFORM, no static
 * reservations. Replies are broadcast, which every client accepts. */
#include "NetworkDhcpServer.h"

#include <Arduino.h>
#include <string.h>

#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

static const uint32_t LEASE_SECONDS = 3600;
static const uint32_t OFFER_HOLD_MS = 10000;    /* an offer reserves its address this long */
static const uint32_t DECLINE_HOLD_MS = 600000; /* an address a client found in use: skipped this long */

enum {
    DISCOVER = 1, OFFER = 2, REQUEST = 3, DECLINE = 4, ACK = 5, NAK = 6, RELEASE = 7,
};
enum {
    OPT_PAD = 0, OPT_MASK = 1, OPT_ROUTER = 3, OPT_DNS = 6, OPT_REQUESTED_IP = 50, OPT_LEASE_TIME = 51,
    OPT_MESSAGE_TYPE = 53, OPT_SERVER_ID = 54, OPT_END = 255,
};
static const uint16_t BOOTP_FIXED = 236;
static const uint32_t MAGIC = 0x63825363u;

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

using Lease = NetworkDhcpServer::Lease;

/* Taken: offered, bound or declined, and not yet past `until`. */
static bool taken(const Lease &l, uint32_t now)
{
    return l.state != NetworkDhcpServer::UNUSED && l.state != NetworkDhcpServer::ENDED && (int32_t)(l.until - now) > 0;
}

/* The option `code` in the options area, or nullptr. *length gets its length. */
static const uint8_t *find_option(const uint8_t *opt, const uint8_t *end, uint8_t code, uint8_t *length)
{
    while (opt < end && *opt != OPT_END) {
        if (*opt == OPT_PAD) {
            opt++;
            continue;
        }
        if (opt + 2 > end || opt + 2 + opt[1] > end) {
            return nullptr;
        }
        if (*opt == code) {
            *length = opt[1];
            return opt + 2;
        }
        opt += 2 + opt[1];
    }
    return nullptr;
}

static int pick(NetworkDhcpServer *s, const uint8_t *mac, uint32_t requested, uint32_t now)
{
    /* The client's own lease, current or ended. */
    for (int i = 0; i < s->count; i++) {
        const Lease &l = s->leases[i];
        if (l.state != NetworkDhcpServer::UNUSED && l.state != NetworkDhcpServer::DECLINED && !memcmp(l.mac, mac, 6)) {
            return i;
        }
    }
    /* The address it asked for, when nobody holds it. */
    if (requested >= s->first && requested < s->first + s->count && !taken(s->leases[requested - s->first], now)) {
        return (int)(requested - s->first);
    }
    /* One never handed out, then the one free the longest. */
    int best = -1;
    for (int i = 0; i < s->count; i++) {
        const Lease &l = s->leases[i];
        if (l.state == NetworkDhcpServer::UNUSED) {
            return i;
        }
        if (!taken(l, now) && (best < 0 || (int32_t)(l.until - s->leases[best].until) < 0)) {
            best = i;
        }
    }
    return best;
}

static void reply(NetworkDhcpServer *s, const uint8_t *request, uint8_t type, uint32_t yiaddr)
{
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, 300, PBUF_RAM);
    if (!p) {
        return;
    }
    uint8_t *m = static_cast<uint8_t *>(p->payload);
    memset(m, 0, 300);
    m[0] = 2;                              /* BOOTREPLY */
    m[1] = 1;
    m[2] = 6;
    memcpy(m + 4, request + 4, 4);         /* xid */
    memcpy(m + 10, request + 10, 2);       /* flags */
    wr32(m + 16, yiaddr);                  /* yiaddr */
    memcpy(m + 28, request + 28, 16);      /* chaddr */
    wr32(m + BOOTP_FIXED, MAGIC);
    uint8_t *o = m + BOOTP_FIXED + 4;
    const uint32_t server = lwip_ntohl(ip4_addr_get_u32(netif_ip4_addr(s->netif)));
    *o++ = OPT_MESSAGE_TYPE; *o++ = 1; *o++ = type;
    *o++ = OPT_SERVER_ID; *o++ = 4; wr32(o, server); o += 4;
    if (type != NAK) {
        *o++ = OPT_LEASE_TIME; *o++ = 4; wr32(o, LEASE_SECONDS); o += 4;
        *o++ = OPT_MASK; *o++ = 4; wr32(o, lwip_ntohl(ip4_addr_get_u32(netif_ip4_netmask(s->netif)))); o += 4;
        if (s->router) {
            *o++ = OPT_ROUTER; *o++ = 4; wr32(o, lwip_ntohl(s->router)); o += 4;
        }
        if (s->dns) {
            *o++ = OPT_DNS; *o++ = 4; wr32(o, lwip_ntohl(s->dns)); o += 4;
        }
    }
    *o++ = OPT_END;
    ip_addr_t to;
    ip_addr_set_ip4_u32(&to, IPADDR_BROADCAST);
    udp_sendto_if(s->pcb, p, &to, 68, s->netif);
    pbuf_free(p);
}

static void on_request(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    (void)pcb;
    (void)addr;
    (void)port;
    auto *s = static_cast<NetworkDhcpServer *>(arg);
    uint8_t m[576];
    const uint16_t length = pbuf_copy_partial(p, m, sizeof m, 0);
    pbuf_free(p);
    if (length < BOOTP_FIXED + 4 + 3 || m[0] != 1 || m[1] != 1 || m[2] != 6 || rd32(m + BOOTP_FIXED) != MAGIC) {
        return;
    }
    if (rd32(m + 24)) {
        return;   /* through a relay (giaddr): not served */
    }
    const uint8_t *opt = m + BOOTP_FIXED + 4, *end = m + length;
    uint8_t n = 0;
    const uint8_t *type = find_option(opt, end, OPT_MESSAGE_TYPE, &n);
    if (!type || n != 1) {
        return;
    }
    const uint8_t *mac = m + 28;
    const uint32_t now = millis();
    const uint32_t ours = lwip_ntohl(ip4_addr_get_u32(netif_ip4_addr(s->netif)));
    const uint8_t *v = find_option(opt, end, OPT_REQUESTED_IP, &n);
    const uint32_t requested = v && n == 4 ? rd32(v) : rd32(m + 12) /* ciaddr */;
    const uint8_t *sid = find_option(opt, end, OPT_SERVER_ID, &n);
    const bool for_us = !sid || (n == 4 && rd32(sid) == ours);

    switch (*type) {
    case DISCOVER: {
        const int i = pick(s, mac, requested, now);
        if (i < 0) {
            return;   /* the pool is full */
        }
        Lease &l = s->leases[i];
        if (!taken(l, now)) {
            memcpy(l.mac, mac, 6);
            l.state = NetworkDhcpServer::OFFERED;
            l.until = now + OFFER_HOLD_MS;
        }
        reply(s, m, OFFER, s->first + (uint32_t)i);
        break;
    }
    case REQUEST: {
        if (!for_us) {
            /* The client took another server's offer: let ours go. */
            for (int i = 0; i < s->count; i++) {
                Lease &l = s->leases[i];
                if (l.state == NetworkDhcpServer::OFFERED && !memcmp(l.mac, mac, 6)) {
                    l.state = NetworkDhcpServer::UNUSED;
                }
            }
            return;
        }
        const bool in_pool = requested >= s->first && requested < s->first + s->count;
        Lease *l = in_pool ? &s->leases[requested - s->first] : nullptr;
        if (!l || (taken(*l, now) && memcmp(l->mac, mac, 6))) {
            if (sid || in_pool) {
                reply(s, m, NAK, 0);   /* not ours to give: the client starts over */
            }
            return;
        }
        memcpy(l->mac, mac, 6);
        l->state = NetworkDhcpServer::BOUND;
        l->until = now + LEASE_SECONDS * 1000u;
        reply(s, m, ACK, requested);
        break;
    }
    case DECLINE:
    case RELEASE: {
        const uint32_t which = *type == DECLINE ? requested : rd32(m + 12);
        if (!for_us || which < s->first || which >= s->first + s->count) {
            return;
        }
        Lease &l = s->leases[which - s->first];
        if (memcmp(l.mac, mac, 6)) {
            return;
        }
        if (*type == DECLINE) {
            memset(l.mac, 0, 6);
            l.state = NetworkDhcpServer::DECLINED;
            l.until = now + DECLINE_HOLD_MS;
        } else {
            l.state = NetworkDhcpServer::ENDED;
            l.until = now;
        }
        break;
    }
    default:
        break;
    }
}

NetworkDhcpServer *NetworkDhcpServer::start(struct netif *n, uint32_t first, uint8_t count, uint32_t router, uint32_t dns)
{
    if (!count) {
        return nullptr;
    }
    const size_t size = sizeof(NetworkDhcpServer) + (count - 1) * sizeof(Lease);
    auto *s = static_cast<NetworkDhcpServer *>(calloc(1, size));
    if (!s) {
        return nullptr;
    }
    s->pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    if (!s->pcb) {
        free(s);
        return nullptr;
    }
    ip_set_option(s->pcb, SOF_BROADCAST | SOF_REUSEADDR);
    udp_bind_netif(s->pcb, n);
    if (udp_bind(s->pcb, IP4_ADDR_ANY, 67) != ERR_OK) {
        udp_remove(s->pcb);
        free(s);
        return nullptr;
    }
    udp_recv(s->pcb, on_request, s);
    s->netif = n;
    s->first = first;
    s->count = count;
    s->router = router;
    s->dns = dns;
    return s;
}

void NetworkDhcpServer::stop()
{
    udp_remove(pcb);
    free(this);
}
