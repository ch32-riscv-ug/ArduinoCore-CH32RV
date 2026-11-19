#include <ch32rv_pioc.inc>

/* A UART transmitter on IO0 (8N1, LSB first, idle high), its bytes in the
 * shared registers: the PIOC keeps running between commands, so the line
 * stays high (loadSram() would hold it in reset, the pad left floating).
 *
 * Each command: d0 the bytes to send (1 .. 24) from d8 .. d31, d1 / d2 the
 * bit time as the turns of the wait below (d1: 1 .. 256, 0 for 256; d2: 0 ..
 * 255; the host works them out from the PIOC's clock). Every bit - start,
 * data, stop - is one output instruction and one call of WAIT_BIT:
 *   3 * d1 + 770 * d2 + 11 cycles,
 * so they all last the same. The next byte's fetch comes after the stop bit,
 * making the idle between bytes a little longer. Then cmd.out 0 with the
 * request set; the line stays high. */
.equ COUNT,   SFR_DATA_REG0
.equ T_LO,    SFR_DATA_REG1
.equ T_HI,    SFR_DATA_REG2
.equ TURN,    SFR_DATA_REG3
.equ OUTER,   SFR_DATA_REG4
.equ FIRST,   SFR_DATA_REG8             // the bytes: d8 .. d31

PIOC_BEGIN uart_tx
        DW     0x0000                   // word 0: reserved
        JMP    START
        ORG    0x0008
// with its CALL and the output instruction before it: 3 * T_LO + 770 * T_HI + 11 cycles
WAIT_BIT:
        MOV    T_LO, A
        MOVA   TURN
        MOV    T_HI, A
        MOVA   OUTER
        INC    OUTER                    // T_HI + 1 rounds: T_LO turns, then 256 each
W1:     DECSZ  TURN
        JMP    W1
        DECSZ  OUTER
        JMP    W1
        RET
// the byte in SFR_DATA_EXCH, framed
SEND:   BC     SFR_PORT_IO, SB_PORT_OUT0   // start bit
        CALL   WAIT_BIT
        BP2F   BO_PORT_OUT0, 0
        CALL   WAIT_BIT
        BP2F   BO_PORT_OUT0, 1
        CALL   WAIT_BIT
        BP2F   BO_PORT_OUT0, 2
        CALL   WAIT_BIT
        BP2F   BO_PORT_OUT0, 3
        CALL   WAIT_BIT
        BP2F   BO_PORT_OUT0, 4
        CALL   WAIT_BIT
        BP2F   BO_PORT_OUT0, 5
        CALL   WAIT_BIT
        BP2F   BO_PORT_OUT0, 6
        CALL   WAIT_BIT
        BP2F   BO_PORT_OUT0, 7
        CALL   WAIT_BIT
        BS     SFR_PORT_IO, SB_PORT_OUT0   // stop bit
        CALL   WAIT_BIT
        RET
START:  BS     SFR_PORT_IO, SB_PORT_OUT0   // idle high
        MOVA1F 0x01                     // IO0 an output
WAIT:   WAITB  WB_DATA_MW_SR_1
        MOV    SFR_CTRL_WR, A           // the command (any)
        MOVL   FIRST
        MOVA   SFR_INDIR_ADDR
        CLRA
        IOR    COUNT, A
        JZ     DONE
NEXT:   MOV    SFR_INDIR_PORT, A        // the byte at SFR_INDIR_ADDR
        MOVA   SFR_DATA_EXCH
        INC    SFR_INDIR_ADDR
        CALL   SEND
        DECSZ  COUNT
        JMP    NEXT
DONE:   CLRA
        MOVA   SFR_CTRL_RD
        BS     SFR_SYS_CFG, SB_INT_REQ
        JMP    WAIT
PIOC_END uart_tx
