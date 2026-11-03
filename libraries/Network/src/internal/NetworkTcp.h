/* The connection behind NetworkClient, shared with NetworkServer. Internal. */
#pragma once
#include <stdint.h>
#include "lwip/tcp.h"

struct NetworkTcpConnection {
    struct tcp_pcb *pcb = nullptr;   /* nullptr once lwIP freed it (closed, reset, aborted) */
    uint8_t *rx = nullptr;           /* ring of TCP_WND bytes */
    uint16_t rxHead = 0, rxCount = 0;
    uint8_t refs = 1;
    bool connecting = false;
    bool established = false;
    bool peerClosed = false;         /* FIN received; what is in rx is the rest */
    int8_t error = 0;                /* the lwIP error that ended it, if any */

    /* A connection for pcb (accepted or about to connect): callbacks attached. */
    static NetworkTcpConnection *attach(struct tcp_pcb *pcb);
    void retain() { refs++; }
    void release();                  /* the last release closes and frees */
    void close();                    /* graceful; aborts if lwIP has no memory for the FIN */
};
