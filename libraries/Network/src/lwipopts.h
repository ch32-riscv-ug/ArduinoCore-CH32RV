/* lwIP's build-time configuration for the Network library.
 *
 * lwIP sizes its pools at build time, and the Arduino IDE builds libraries
 * without a sketch's defines, so the configuration is picked here from the
 * series, by how much RAM it has - never from a sketch. Two sizes:
 *
 *   NETWORK_LWIP_SMALL  64 KB of RAM  (CH32V208, V203RB, V307/V317)
 *   NETWORK_LWIP_LARGE  more          (CH32V407/V467, H415-H417, the host test build,
 *                                      V307/V317 with 128 KB or more in the Flash / RAM Split menu)
 *
 * The split menu's RAM reaches every compile as CH32RV_RAMSPLIT_RAM_KB (boards.txt).
 * 96 KB stays small: the large pools alone take about 60 KB.
 *
 * IPv4 only. The pools below bound what the stack itself takes; each
 * NetworkClient also allocates a receive buffer of TCP_WND bytes while it is
 * open. */
#pragma once
#include <stdint.h>

#if (defined(CH32RV_SERIES_CH32V307) || defined(CH32RV_SERIES_CH32V317)) && defined(CH32RV_RAMSPLIT_RAM_KB) && \
    CH32RV_RAMSPLIT_RAM_KB >= 128
#define NETWORK_LWIP_LARGE 1
#elif defined(CH32RV_SERIES_CH32V208) || defined(CH32RV_PART_CH32V203RBT6) || defined(CH32RV_SERIES_CH32V307) || \
    defined(CH32RV_SERIES_CH32V317)
#define NETWORK_LWIP_SMALL 1
#elif defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467) || defined(CH32RV_SERIES_CH32H415) || \
    defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H417) || defined(NETWORK_HOST_TEST)
#define NETWORK_LWIP_LARGE 1
#else
#error "The Network library is for the series with an Ethernet MAC: CH32V208, V203RB, V307/V317, V407/V467, H415-H417 (docs/network-stack.ja.md)"
#endif

/* No OS: raw API only, called from the task (ch32rv_net_task) and never from an interrupt. */
#define NO_SYS 1
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define SYS_LIGHTWEIGHT_PROT 0
#define LWIP_TIMERS 1

#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define LWIP_ICMP 1
#define LWIP_RAW 0
#define LWIP_UDP 1
#define LWIP_TCP 1
#define LWIP_DHCP 1
#define LWIP_DHCP_DOES_ACD_CHECK 0
#define LWIP_DNS 1
#define LWIP_IGMP 1                 /* NetworkUDP::beginMulticast(), and mDNS later */
#define LWIP_NETIF_HOSTNAME 1       /* sent as DHCP option 12 */
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_EXT_STATUS_CALLBACK 1   /* mDNS re-announces when an address changes */
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_LOOPBACK 1       /* to the interface's own address; ch32rv_net_task() polls the queue */
#define LWIP_HAVE_LOOPIF 1          /* 127.0.0.1 */
#define LWIP_LOOPBACK_MAX_PBUFS 8
#define LWIP_STATS 0
#ifndef NETWORK_HOST_TEST
#define LWIP_NOASSERT 1             /* lwIP's asserts guard its own invariants; on the chip they would only hang */
#endif
#define LWIP_CHECKSUM_CTRL_PER_NETIF 1   /* the Ethernet MAC checks and inserts its own; USB NCM keeps lwIP's */
#define IP_REASSEMBLY 1
#define IP_FRAG 1
#define IP_FORWARD 0
#define LWIP_TCP_KEEPALIVE 1
#define SO_REUSE 1                  /* a server restarted on the same port while old connections linger */
#define TCP_LISTEN_BACKLOG 1
#define DNS_TABLE_SIZE 4
#define DNS_MAX_NAME_LENGTH 128
#define DNS_MAX_SERVERS 2
#define ARP_TABLE_SIZE 8
#define ARP_QUEUEING 1
#define ETHARP_SUPPORT_STATIC_ENTRIES 1   /* etharp_add_static_entry(): a peer pinned to one MAC (a host that answers ARP on two NICs) */

/* The mDNS responder (NetworkMdns / MDNS) and the SNTP client (configTime()).
 * Linked only when a sketch uses them. */
#define LWIP_MDNS_RESPONDER 1
#define MDNS_MAX_SERVICES 4
#define LWIP_NUM_NETIF_CLIENT_DATA 1        /* mDNS keeps its per-interface state there */
#define SNTP_SERVER_DNS 1
#define SNTP_MAX_SERVERS 3
#define SNTP_STARTUP_DELAY 0
#define SNTP_SET_SYSTEM_TIME_US(sec, us) network_sntp_set_time((uint32_t)(sec), (uint32_t)(us))
#define MEMP_NUM_SYS_TIMEOUT (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 8)

#if UINTPTR_MAX > 0xFFFFFFFFu
#define MEM_ALIGNMENT 8             /* the host test build */
#else
#define MEM_ALIGNMENT 4
#endif

#define TCP_MSS 1460
#define PBUF_POOL_BUFSIZE 1536      /* one whole frame per pbuf */

#if defined(NETWORK_LWIP_SMALL)
#define MEM_SIZE (10 * 1024)        /* transmit data, copied in by tcp_write() and udp */
#define PBUF_POOL_SIZE 6            /* receive frames */
#define TCP_WND (2 * TCP_MSS)
#define TCP_SND_BUF (2 * TCP_MSS)
#define MEMP_NUM_TCP_PCB 6
#define MEMP_NUM_TCP_PCB_LISTEN 4
#define MEMP_NUM_UDP_PCB 6
#define MEMP_NUM_TCP_SEG 16
#define MEMP_NUM_PBUF 8
#else
#define MEM_SIZE (32 * 1024)
#define PBUF_POOL_SIZE 16
#define TCP_WND (4 * TCP_MSS)
#define TCP_SND_BUF (4 * TCP_MSS)
#define MEMP_NUM_TCP_PCB 12
#define MEMP_NUM_TCP_PCB_LISTEN 8
#define MEMP_NUM_UDP_PCB 10
#define MEMP_NUM_TCP_SEG 32
#define MEMP_NUM_PBUF 16
#endif
#define TCP_SND_QUEUELEN ((4 * TCP_SND_BUF + TCP_MSS - 1) / TCP_MSS)

/* TCP initial sequence numbers and the DHCP / DNS transaction ids. */
#define LWIP_RAND() ((u32_t)network_random())
#ifdef __cplusplus
extern "C"
#endif
uint32_t network_random(void);
#ifdef __cplusplus
extern "C"
#endif
void network_sntp_set_time(uint32_t sec, uint32_t us);
