#include <ch32rv_pioc.inc>
/* A copy of libraries/CH32RVPioc/examples/PiocWs2812/ws2812.S (tests/unit checks they match). */

/* WS2812 (NeoPixel) frames from the PIOC: the bytes the host put in the
 * program SRAM at byte 0x400 (word 0x200; CH32RVPioc::loadSram()), MSB
 * first, low byte of each word first - the bytes in the order they lie.
 * IO0 an output, low between frames.
 *
 * Each command: d0 / d1 bytes to send (low, high), d2 / d3 / d4 the turns
 * of a 3-cycle loop for the high time of a 0, the further high time of a 1
 * and the low rest, d5 the low rest of a byte's last bit (each 1 .. 255; the
 * host works them out from the PIOC's clock). Counted in the PIOC's cycles
 * from the rising edge:
 *   a 0 falls at 3 * d2 + 4, a 1 at 3 * (d2 + d3) + 6,
 *   the next bit rises at 3 * (d2 + d3 + d4) + 13 (both kinds alike);
 *   after a byte's last bit at 3 * (d2 + d3 + d5) + 29, and 12 more where
 *   the next word is fetched (every second byte) - so d5 = d4 - 5 keeps the
 *   period, and the fetch adds 12 cycles.
 * Then cmd.out 0 with the request set; the line stays low (the latch is the
 * host's wait of 50 us or more). */
.equ CNT_L,   SFR_DATA_REG0
.equ CNT_H,   SFR_DATA_REG1
.equ T_H0,    SFR_DATA_REG2
.equ T_H1,    SFR_DATA_REG3
.equ T_LOW,   SFR_DATA_REG4
.equ T_LAST,  SFR_DATA_REG5
.equ ADDR_L,  SFR_DATA_REG6
.equ ADDR_H,  SFR_DATA_REG7
.equ BITS,    SFR_DATA_REG8
.equ TURN,    SFR_DATA_REG9
.equ BYTE,    SFR_DATA_REG10
.equ HIBYTE,  SFR_DATA_REG11

PIOC_BEGIN ws2812
        DW     0x0000                   // word 0: reserved
        JMP    START
        ORG    0x0008
// one byte from BYTE, MSB first
SEND8:  MOVL   8
        MOVA   BITS
BIT:    BS     SFR_PORT_IO, SB_PORT_OUT0
        MOV    T_H0, A
        MOVA   TURN
H0:     DECSZ  TURN
        JMP    H0
        RCL    BYTE                     // the bit to send into C
        BTSS   SFR_STATUS_REG, SB_FLAG_C
        BC     SFR_PORT_IO, SB_PORT_OUT0   // a 0 falls here
        MOV    T_H1, A
        MOVA   TURN
H1:     DECSZ  TURN
        JMP    H1
        BC     SFR_PORT_IO, SB_PORT_OUT0   // a 1 falls here
        DECSZ  BITS
        JMP    NEXT
        MOV    T_LAST, A                // the byte's last bit: what follows takes the rest
        MOVA   TURN
LAST:   DECSZ  TURN
        JMP    LAST
        RET
NEXT:   MOV    T_LOW, A
        MOVA   TURN
LO:     DECSZ  TURN
        JMP    LO
        JMP    BIT
// CNT_H:CNT_L one less; Z when it reached 0
COUNT:  DEC    CNT_L
        INC    CNT_L, A                 // A = the count before: 0 borrows
        BTSC   SFR_STATUS_REG, SB_FLAG_Z
        DEC    CNT_H
        MOV    CNT_L, A
        IOR    CNT_H, A
        RET
START:  BC     SFR_PORT_IO, SB_PORT_OUT0
        MOVA1F 0x01                     // IO0 an output
WAIT:   WAITB  WB_DATA_MW_SR_1
        MOV    SFR_CTRL_WR, A           // the command (any)
        CLR    ADDR_L
        MOVL   0x02
        MOVA   ADDR_H
        MOV    CNT_L, A
        IOR    CNT_H, A
        JZ     DONE
WORD:   MOV    ADDR_L, A
        MOVA   SFR_INDIR_ADDR
        MOV    ADDR_H, A
        RDCODE                          // A: the low byte, SFR_INDIR_ADDR: the high
        MOVA   BYTE
        MOV    SFR_INDIR_ADDR, A
        MOVA   HIBYTE
        CALL   SEND8
        CALL   COUNT
        JZ     DONE
        MOV    HIBYTE, A
        MOVA   BYTE
        CALL   SEND8
        CALL   COUNT
        JZ     DONE
        INC    ADDR_L
        BTSC   SFR_STATUS_REG, SB_FLAG_Z
        INC    ADDR_H
        JMP    WORD
DONE:   CLRA
        MOVA   SFR_CTRL_RD
        BS     SFR_SYS_CFG, SB_INT_REQ
        JMP    WAIT
PIOC_END ws2812
