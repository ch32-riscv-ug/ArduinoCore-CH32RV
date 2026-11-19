#include <ch32rv_pioc.inc>

/* A UART receiver on IO0 (8N1, LSB first, idle high): up to 24 bytes into
 * the shared registers d8 .. d31.
 *
 * Each command: d0 the bytes to take (1 .. 24); d1 / d2 the bit time and
 * d3 / d4 the wait from the start bit's falling edge to the middle of bit 0,
 * as the turns of the wait below (low byte 1 .. 256, 0 for 256; high byte
 * 0 .. 255; the host works them out from the PIOC's clock). A bit is sampled
 * (BG2F, one cycle) every
 *   3 * d1 + 770 * d2 + 11 cycles,
 * the first one 3 * d3 + 770 * d4 + 12 cycles (give or take 1) after the
 * edge is seen. Each byte waits for the line high (the stop bit or idle),
 * then for the next start bit. d7 counts the bytes taken; when d0 are in,
 * cmd.out 0 with the request set. Without them it waits on: the host times
 * out, reads d7, and begins it again. */
.equ COUNT,   SFR_DATA_REG0
.equ B_LO,    SFR_DATA_REG1
.equ B_HI,    SFR_DATA_REG2
.equ F_LO,    SFR_DATA_REG3
.equ F_HI,    SFR_DATA_REG4
.equ TURN,    SFR_DATA_REG5
.equ OUTER,   SFR_DATA_REG6
.equ GOT,     SFR_DATA_REG7
.equ FIRST,   SFR_DATA_REG8             // the bytes: d8 .. d31

PIOC_BEGIN uart_rx
        DW     0x0000                   // word 0: reserved
        JMP    START
        ORG    0x0008
// with its CALL: 3 * B_LO + 770 * B_HI + 10 cycles
WAIT_B: MOV    B_LO, A
        MOVA   TURN
        MOV    B_HI, A
        MOVA   OUTER
        INC    OUTER                    // B_HI + 1 rounds: B_LO turns, then 256 each
WB1:    DECSZ  TURN
        JMP    WB1
        DECSZ  OUTER
        JMP    WB1
        RET
// with its CALL: 3 * F_LO + 770 * F_HI + 10 cycles
WAIT_F: MOV    F_LO, A
        MOVA   TURN
        MOV    F_HI, A
        MOVA   OUTER
        INC    OUTER
WF1:    DECSZ  TURN
        JMP    WF1
        DECSZ  OUTER
        JMP    WF1
        RET
START:  MOVA1F 0x00                     // IO0 (and IO1) an input
WAIT:   WAITB  WB_DATA_MW_SR_1
        MOV    SFR_CTRL_WR, A           // the command (any)
        MOVL   FIRST
        MOVA   SFR_INDIR_ADDR
        CLR    GOT
NEXT:
IDLE:   BTSS   SFR_PORT_IO, SB_PORT_IN0    // on when the line is high
        JMP    IDLE
EDGE:   BTSC   SFR_PORT_IO, SB_PORT_IN0    // on at the start bit's falling edge
        JMP    EDGE
        CALL   WAIT_F
        BG2F   BI_PORT_IN0, 0
        CALL   WAIT_B
        BG2F   BI_PORT_IN0, 1
        CALL   WAIT_B
        BG2F   BI_PORT_IN0, 2
        CALL   WAIT_B
        BG2F   BI_PORT_IN0, 3
        CALL   WAIT_B
        BG2F   BI_PORT_IN0, 4
        CALL   WAIT_B
        BG2F   BI_PORT_IN0, 5
        CALL   WAIT_B
        BG2F   BI_PORT_IN0, 6
        CALL   WAIT_B
        BG2F   BI_PORT_IN0, 7
        MOV    SFR_DATA_EXCH, A
        MOVA   SFR_INDIR_PORT           // into the next of d8 .. d31
        INC    SFR_INDIR_ADDR
        INC    GOT
        DECSZ  COUNT
        JMP    NEXT
        CLRA
        MOVA   SFR_CTRL_RD
        BS     SFR_SYS_CFG, SB_INT_REQ
        JMP    WAIT
PIOC_END uart_rx
