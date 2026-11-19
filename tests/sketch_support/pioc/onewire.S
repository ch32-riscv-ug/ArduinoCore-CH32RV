#include <ch32rv_pioc.inc>
/* A copy of libraries/CH32RVPioc/examples/PiocOneWire/onewire.S (tests/unit checks they match). */

/* A 1-Wire master on IO0 (standard speed), the pad open drain (the host's CH32RVPioc::setOpenDrain(0)): an output
 * 0 holds the line low, a 1 lets it go to the pull-up; it is never driven high. The PIOC keeps running between
 * commands with the line let go.
 *
 * Time is counted in microseconds: d1 turns of 3 cycles and about 4 more make one (the host works d1 out from the
 * PIOC's clock). The command picks what to do:
 *   1  reset: low 500 us (480 at least, with room for the clock's spread), let go, the line looked at 70 us later, 410 us more. cmd.out 0 with a presence pulse,
 *      1 without (the line still high)
 *   2  write d0 bytes (1 .. 24) from d8 .. d31, LSB first: a 1 is low 6 us then 64 us let go, a 0 low 60 us then 10
 *   3  read d0 bytes into d8 .. d31, LSB first: low 6 us, let go, the line looked at 9 us later, 55 us more
 * Each ends with the request set (cmd.out 0 for writes and reads). */
.equ COUNT,   SFR_DATA_REG0
.equ US,      SFR_DATA_REG1
.equ T0,      SFR_DATA_REG2
.equ T1,      SFR_DATA_REG3
.equ CMD,     SFR_DATA_REG4
.equ BYTE,    SFR_DATA_REG5
.equ NBIT,    SFR_DATA_REG6
.equ RES,     SFR_DATA_REG7
.equ FIRST,   SFR_DATA_REG8             // the bytes: d8 .. d31

PIOC_BEGIN onewire
        DW     0x0000                   // word 0: reserved
        JMP    START
        ORG    0x0008
// A microseconds (1 .. 255): A x (3 x US + 4) cycles and the call
DLY:    MOVA   T1
D1:     MOV    US, A
        MOVA   T0
D0:     DECSZ  T0
        JMP    D0
        DECSZ  T1
        JMP    D1
        RET
.macro  LOW                             // hold the line low
        BC     SFR_PORT_IO, SB_PORT_OUT0
.endm
.macro  LETGO                           // let it go (the pull-up takes it high)
        BS     SFR_PORT_IO, SB_PORT_OUT0
.endm
.macro  WAITUS n
        MOVL   \n
        CALL   DLY
.endm

START:  LETGO
        MOVA1F 0x01                     // IO0 an output (open drain at the pad)
WAIT:   WAITB  WB_DATA_MW_SR_1
        MOV    SFR_CTRL_WR, A
        MOVA   CMD
        MOVL   FIRST
        MOVA   SFR_INDIR_ADDR
        BTSS   CMD, 1
        JMP    RESET
        BTSC   CMD, 0
        JMP    READ

WRITE:  MOV    SFR_INDIR_PORT, A
        MOVA   BYTE
        INC    SFR_INDIR_ADDR
        MOVL   8
        MOVA   NBIT
WBIT:   RCR    BYTE                     // the next bit (LSB first) into C
        BTSC   SFR_STATUS_REG, SB_FLAG_C
        JMP    W1
        LOW                             // a 0: low 60 us, 10 let go
        WAITUS 60
        LETGO
        WAITUS 10
        JMP    WNEXT
W1:     LOW                             // a 1: low 6 us, 64 let go
        WAITUS 6
        LETGO
        WAITUS 64
WNEXT:  DECSZ  NBIT
        JMP    WBIT
        DECSZ  COUNT
        JMP    WRITE
        CLRA
        JMP    DONE

READ:   MOVL   8
        MOVA   NBIT
RBIT:   LOW
        WAITUS 6
        LETGO
        WAITUS 9
        BC     SFR_STATUS_REG, SB_FLAG_C
        BTSC   SFR_PORT_IO, SB_PORT_IN0
        BS     SFR_STATUS_REG, SB_FLAG_C
        RCR    BYTE                     // in at the top: after 8, the first bit is bit 0
        WAITUS 55
        DECSZ  NBIT
        JMP    RBIT
        MOV    BYTE, A
        MOVA   SFR_INDIR_PORT
        INC    SFR_INDIR_ADDR
        DECSZ  COUNT
        JMP    READ
        CLRA
        JMP    DONE

RESET:  LOW
        WAITUS 250
        WAITUS 250
        LETGO
        WAITUS 70
        CLR    RES
        BTSC   SFR_PORT_IO, SB_PORT_IN0    // still high: nobody answered
        INC    RES
        WAITUS 205
        WAITUS 205
        MOV    RES, A
DONE:   MOVA   SFR_CTRL_RD
        BS     SFR_SYS_CFG, SB_INT_REQ
        JMP    WAIT
PIOC_END onewire
