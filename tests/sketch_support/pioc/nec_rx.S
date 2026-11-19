#include <ch32rv_pioc.inc>
/* A copy of libraries/CH32RVPioc/examples/PiocIrNecRx/nec_rx.S (tests/unit checks they match). */

/* An NEC infrared receiver: IO0 (an input) from an IR receiver module's output (38 kHz demodulated: low while the
 * carrier is on, high between). One command decodes one frame into d8 .. d11 (the 32 bits in the order they came,
 * LSB first: d8 the address, d9 its inverse or the extended address's high byte, d10 the command, d11 its inverse)
 * while the main core goes on.
 *
 * Time is counted in ticks: d1 x 256 turns of 3 cycles (d1 from the host: about 75 us). A level is looked at once a
 * tick, so a time is known within a tick. Each command:
 *   d2  256 - the ticks from which a bit's space is a 1 (between 562.5 us and 1687.5 us)
 *   d3  256 - the ticks from which the leader's space is a frame's (4.5 ms), not a repeat's (2.25 ms)
 *   d4  256 - the ticks the leader's mark needs at least (9 ms; a shorter low is noise and waited past)
 * Waits for the line high, then for the leader; a level held for 256 ticks ends the frame as a timeout. cmd.out:
 * 0 a frame in d8 .. d11, 1 a repeat code, 2 a timeout in the middle; each with the request set. Without a frame it
 * waits on; the host times out and begins it again. */
.equ M,        SFR_DATA_REG1
.equ THR_BIT,  SFR_DATA_REG2
.equ THR_LEAD, SFR_DATA_REG3
.equ MIN_MARK, SFR_DATA_REG4
.equ B0,       SFR_DATA_REG8
.equ B1,       SFR_DATA_REG9
.equ B2,       SFR_DATA_REG10
.equ B3,       SFR_DATA_REG11
.equ T0,       SFR_DATA_REG12
.equ T1,       SFR_DATA_REG13
.equ CNT,      SFR_DATA_REG14          // ticks of this level; wrapping to 0 is the timeout
.equ K,        SFR_DATA_REG15          // counts up from 256 - threshold; wrapping to 0 sets LONG
.equ NB,       SFR_DATA_REG16          // bits left
.equ FLAG,     SFR_DATA_REG17
.equ LONG,     0                       // FLAG: the level lasted the threshold
.equ TOUT,     1                       // FLAG: it lasted 256 ticks

PIOC_BEGIN nec_rx
        DW     0x0000                   // word 0: reserved
        JMP    START
        ORG    0x0008
// one tick: M x 256 turns of 3 cycles
TICK:   MOV    M, A
        MOVA   T1
TK1:    CLR    T0
TK0:    DECSZ  T0
        JMP    TK0
        DECSZ  T1
        JMP    TK1
        RET
// ticks while IO0 stays low (MLOW) or high (MHIGH); A: 256 - the threshold for LONG
MLOW:   MOVA   K
        CLR    CNT
        CLR    FLAG
ML1:    CALL   TICK
        INCSZ  CNT
        JMP    ML2
        BS     FLAG, TOUT
        RET
ML2:    INCSZ  K
        JMP    ML3
        BS     FLAG, LONG
ML3:    BTSS   SFR_PORT_IO, SB_PORT_IN0    // on while low
        JMP    ML1
        RET
MHIGH:  MOVA   K
        CLR    CNT
        CLR    FLAG
MH1:    CALL   TICK
        INCSZ  CNT
        JMP    MH2
        BS     FLAG, TOUT
        RET
MH2:    INCSZ  K
        JMP    MH3
        BS     FLAG, LONG
MH3:    BTSC   SFR_PORT_IO, SB_PORT_IN0    // on while high
        JMP    MH1
        RET

START:  MOVA1F 0x00                     // IO0 an input
WAIT:   WAITB  WB_DATA_MW_SR_1
        MOV    SFR_CTRL_WR, A           // the command (any)
IDLE:   BTSS   SFR_PORT_IO, SB_PORT_IN0    // the line high (idle) first
        JMP    IDLE
FALL:   BTSC   SFR_PORT_IO, SB_PORT_IN0    // then a mark begins
        JMP    FALL
        MOV    MIN_MARK, A
        CALL   MLOW
        BTSC   FLAG, TOUT
        JMP    TIMEOUT
        BTSS   FLAG, LONG               // too short for a leader: wait for the next
        JMP    FALL
        MOV    THR_LEAD, A
        CALL   MHIGH
        BTSC   FLAG, TOUT
        JMP    TIMEOUT
        BTSS   FLAG, LONG
        JMP    REPEAT
        MOVL   32
        MOVA   NB
BIT:    CLRA
        CALL   MLOW                     // the bit's mark
        BTSC   FLAG, TOUT
        JMP    TIMEOUT
        MOV    THR_BIT, A
        CALL   MHIGH                    // its space: long for a 1
        BTSC   FLAG, TOUT
        JMP    TIMEOUT
        BC     SFR_STATUS_REG, SB_FLAG_C
        BTSC   FLAG, LONG
        BS     SFR_STATUS_REG, SB_FLAG_C
        RCR    B3                       // in at d11's top: after 32, the first bit is d8's bit 0
        RCR    B2
        RCR    B1
        RCR    B0
        DECSZ  NB
        JMP    BIT
        CLRA
        JMP    DONE
REPEAT: MOVL   1
        JMP    DONE
TIMEOUT: MOVL  2
DONE:   MOVA   SFR_CTRL_RD
        BS     SFR_SYS_CFG, SB_INT_REQ
        JMP    WAIT
PIOC_END nec_rx
