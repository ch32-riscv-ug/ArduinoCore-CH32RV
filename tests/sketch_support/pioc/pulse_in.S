#include <ch32rv_pioc.inc>
/* A copy of libraries/CH32RVPioc/examples/PiocPulseIn/pulse_in.S (tests/unit checks they match). */

/* pulseIn() by the PIOC: one period of the signal on IO0 (an input), the
 * high and the low that follows it, counted in turns of 8 cycles - the main
 * core's interrupts do not touch the count.
 *
 * Each command: waits for IO0 low, then high (a rising edge), counts while
 * it stays high into d1 .. d3 (24 bits, low byte first), then while it stays
 * low into d4 .. d6. Every turn of either count is 8 cycles whether a byte
 * carries or not, so a high of h turns lasted about 8 * h cycles (the edge is
 * seen within a turn, give or take 3 cycles). 24 bits of 8 cycles: about 2.8 s
 * at 48 MHz. Then cmd.out 0 with the request set. Without edges it waits on;
 * the host times out and begins it again. */
.equ H0,      SFR_DATA_REG1
.equ H1,      SFR_DATA_REG2
.equ H2,      SFR_DATA_REG3
.equ L0,      SFR_DATA_REG4
.equ L1,      SFR_DATA_REG5
.equ L2,      SFR_DATA_REG6

PIOC_BEGIN pulse_in
        DW     0x0000                   // word 0: reserved
        JMP    START
        ORG    0x0008
START:  MOVA1F 0x00                     // IO0 (and IO1) an input
WAIT:   WAITB  WB_DATA_MW_SR_1
        MOV    SFR_CTRL_WR, A           // the command (any)
        CLR    H0
        CLR    H1
        CLR    H2
        CLR    L0
        CLR    L1
        CLR    L2
W_LOW:  BTSC   SFR_PORT_IO, SB_PORT_IN0    // on when it is low
        JMP    W_LOW
W_HIGH: BTSS   SFR_PORT_IO, SB_PORT_IN0    // on at the rising edge
        JMP    W_HIGH
HIGH:   INC    H0                       // 5 cycles whatever carries: 1 + 2 + 2, 1 + 1 + 1 + 2 or 1 + 1 + 1 + 1 + 1
        BTSC   SFR_STATUS_REG, SB_FLAG_Z
        INC    H1
        BTSC   SFR_STATUS_REG, SB_FLAG_Z
        INC    H2
        BTSC   SFR_PORT_IO, SB_PORT_IN0    // 1 while high, then the JMP's 2
        JMP    HIGH
LOW:    INC    L0
        BTSC   SFR_STATUS_REG, SB_FLAG_Z
        INC    L1
        BTSC   SFR_STATUS_REG, SB_FLAG_Z
        INC    L2
        BTSS   SFR_PORT_IO, SB_PORT_IN0    // 1 while low, then the JMP's 2
        JMP    LOW
        CLRA
        MOVA   SFR_CTRL_RD
        BS     SFR_SYS_CFG, SB_INT_REQ
        JMP    WAIT
PIOC_END pulse_in
