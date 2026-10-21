#define _GNU_SOURCE
#include "link.h"
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define QN 512
typedef struct { uint64_t at; uint16_t len; uint8_t d[1518]; } item_t;
typedef struct { item_t q[QN]; unsigned head, tail; uint64_t busy_until; unsigned long sent, lost, reord, drop; } dir_t;
static dir_t up, down;          /* up: stack -> host, down: host -> stack */
static int fd = -1;
static link_rx_t rx_cb;
static int (*rx_ready)(void);

void link_rx_gate(int (*ready)(void)) { rx_ready = ready; }
static long kbps, delay_us, loss_ppm, reorder_ppm;
static uint64_t rng = 88172645463325252ull;

static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)rng; }
uint64_t link_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u; }
uint32_t link_ms(void) { return (uint32_t)(link_us() / 1000u); }
static long env(const char *n, long d) { const char *v = getenv(n); return v ? atol(v) : d; }

void link_open(const char *ifname, link_rx_t rx)
{
    rx_cb = rx;
    kbps = env("LINK_KBPS", 0); delay_us = env("LINK_DELAY_US", 0);
    loss_ppm = env("LINK_LOSS_PPM", 0); reorder_ppm = env("LINK_REORDER_PPM", 0);
    rng ^= (uint64_t)env("LINK_SEED", 1) * 0x9E3779B97F4A7C15ull;
    fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK);
    if (fd < 0) { perror("tun"); exit(2); }
    struct ifreq ifr; memset(&ifr, 0, sizeof ifr);
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(fd, TUNSETIFF, &ifr) < 0) { perror("TUNSETIFF"); exit(2); }
}

static void enqueue(dir_t *d, const uint8_t *f, size_t len)
{
    if (len > 1518) len = 1518;
    if (loss_ppm && rnd() % 1000000u < (uint32_t)loss_ppm) { d->lost++; return; }
    if ((d->tail - d->head) >= QN) { d->drop++; return; }      /* link queue full: tail drop */
    const uint64_t now = link_us();
    uint64_t start = d->busy_until > now ? d->busy_until : now;
    uint64_t ser = kbps ? (uint64_t)(len + 24) * 8u * 1000u / (uint64_t)kbps : 0;   /* + preamble/IFG/FCS */
    d->busy_until = start + ser;
    item_t *it = &d->q[d->tail % QN];
    it->at = d->busy_until + (uint64_t)delay_us;
    it->len = (uint16_t)len; memcpy(it->d, f, len);
    d->tail++; d->sent++;
    /* reorder: swap with the previous queued frame */
    if (reorder_ppm && d->tail - d->head >= 2 && rnd() % 1000000u < (uint32_t)reorder_ppm) {
        item_t *pv = &d->q[(d->tail - 2) % QN], tmp = *pv; uint64_t a = pv->at;
        *pv = *it; pv->at = a; tmp.at = it->at; *it = tmp; d->reord++;
    }
}

void link_send(const uint8_t *frame, size_t len) { enqueue(&up, frame, len); }

void link_poll(void)
{
    uint8_t buf[2048];
    for (int i = 0; i < 64; i++) {
        ssize_t n = read(fd, buf, sizeof buf);
        if (n <= 0) break;
        enqueue(&down, buf, (size_t)n);
    }
    const uint64_t now = link_us();
    while (up.head != up.tail && up.q[up.head % QN].at <= now) {
        item_t *it = &up.q[up.head % QN];
        if (write(fd, it->d, it->len) < 0) { /* host queue full */ }
        up.head++;
    }
    while (down.head != down.tail && down.q[down.head % QN].at <= now && (!rx_ready || rx_ready())) {
        item_t *it = &down.q[down.head % QN];
        down.head++;
        rx_cb(it->d, it->len);
    }
}

void link_stats(void)
{
    printf("LINK up sent=%lu lost=%lu reord=%lu drop=%lu down sent=%lu lost=%lu reord=%lu drop=%lu\n",
           up.sent, up.lost, up.reord, up.drop, down.sent, down.lost, down.reord, down.drop);
    fflush(stdout);
}
