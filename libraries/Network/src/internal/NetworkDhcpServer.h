/* The DHCP server behind NetworkInterface::dhcpServer(). Internal. */
#pragma once
#include <stdint.h>

struct netif;
struct udp_pcb;

struct NetworkDhcpServer {
    enum State : uint8_t {
        UNUSED,                /* never handed out */
        OFFERED,               /* reserved by an OFFER until `until` */
        BOUND,                 /* leased until `until` */
        ENDED,                 /* expired or released; kept so the same MAC gets it back */
        DECLINED,              /* a client found it in use: skipped until `until` */
    };
    struct Lease {
        uint8_t mac[6];
        State state;
        uint32_t until;        /* millis() */
    };

    /* Serves on n: addresses first+0 .. first+count-1 (host byte order), the
     * netif's own address as the server id. router / dns are 0 when not offered. */
    static NetworkDhcpServer *start(struct netif *n, uint32_t first, uint8_t count, uint32_t router, uint32_t dns);
    void stop();               /* frees this */

    struct netif *netif;
    struct udp_pcb *pcb;
    uint32_t first;            /* host byte order */
    uint32_t router, dns;      /* network byte order, 0 = not offered */
    uint8_t count;
    Lease leases[1];           /* count of them, allocated with the server */
};
