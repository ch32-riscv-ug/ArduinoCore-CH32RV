#include <ch32rv_pioc.inc>

/* An NEC infrared remote's frame on IO0, as an IR LED is driven: the 38 kHz
 * carrier on for a mark, IO0 low for a space and between frames.
 *
 * Each command: d0 .. d3 the frame's four bytes as sent (address, its
 * inverse or the extended address's high byte, command, its inverse; LSB
 * first), d4 / d5 the carrier's high part and d6 / d7 its low part as the
 * turns of the wait below (low byte 1 .. 256, 0 for 256; high byte 0 .. 255;
 * the host works them out from the PIOC's clock). One carrier period is
 *   (3 * d4 + 770 * d5) + (3 * d6 + 770 * d7) + 25 cycles, high for the first
 *   3 * d4 + 770 * d5 + 11 of them;
 * a mark and a space are counted in those periods, so at 38 kHz:
 *   the leader 342 marks (9.0 ms) and 171 spaces (4.5 ms), a bit a mark of
 *   21 (553 us) and 21 (0) or 64 (1684 us) spaces, a closing mark of 21.
 * Then cmd.out 0 with the request set; IO0 stays low. */
.equ B0,      SFR_DATA_REG0             // the frame's bytes: d0 .. d3
.equ H_LO,    SFR_DATA_REG4
.equ H_HI,    SFR_DATA_REG5
.equ L_LO,    SFR_DATA_REG6
.equ L_HI,    SFR_DATA_REG7
.equ N,       SFR_DATA_REG8
.equ TURN,    SFR_DATA_REG9
.equ OUTER,   SFR_DATA_REG10
.equ BYTES,   SFR_DATA_REG11
.equ BITS,    SFR_DATA_REG12
.equ CUR,     SFR_DATA_REG13

PIOC_BEGIN ir_nec
        DW     0x0000                   // word 0: reserved
        JMP    START
        ORG    0x0008
// with its CALL: 3 * H_LO + 770 * H_HI + 10 cycles
WAIT_H: MOV    H_LO, A
        MOVA   TURN
        MOV    H_HI, A
        MOVA   OUTER
        INC    OUTER                    // H_HI + 1 rounds: H_LO turns, then 256 each
WH1:    DECSZ  TURN
        JMP    WH1
        DECSZ  OUTER
        JMP    WH1
        RET
// with its CALL: 3 * L_LO + 770 * L_HI + 10 cycles
WAIT_L: MOV    L_LO, A
        MOVA   TURN
        MOV    L_HI, A
        MOVA   OUTER
        INC    OUTER
WL1:    DECSZ  TURN
        JMP    WL1
        DECSZ  OUTER
        JMP    WL1
        RET
// A carrier periods (1 .. 255): the carrier where SFR_DATA_EXCH bit 0 is 1 (a mark), IO0 low where it is 0 (a
// space); both take the same cycles
PERIODS:
        MOVA   N
P1:     BP2F   BO_PORT_OUT0, 0
        CALL   WAIT_H
        BC     SFR_PORT_IO, SB_PORT_OUT0
        CALL   WAIT_L
        DECSZ  N
        JMP    P1
        RET
SPACE:  CLR    SFR_DATA_EXCH
        JMP    PERIODS
START:  BC     SFR_PORT_IO, SB_PORT_OUT0
        MOVA1F 0x01                     // IO0 an output
WAIT:   WAITB  WB_DATA_MW_SR_1
        MOV    SFR_CTRL_WR, A           // the command (any)
        MOVL   1
        MOVA   SFR_DATA_EXCH
        MOVL   171                      // the leader: 342 marks, 171 spaces
        CALL   PERIODS
        MOVL   171
        CALL   PERIODS
        MOVL   171
        CALL   SPACE
        MOVL   B0
        MOVA   SFR_INDIR_ADDR
        MOVL   4
        MOVA   BYTES
BYTE:   MOV    SFR_INDIR_PORT, A
        MOVA   CUR
        INC    SFR_INDIR_ADDR
        MOVL   8
        MOVA   BITS
BIT:    MOVL   1
        MOVA   SFR_DATA_EXCH
        MOVL   21
        CALL   PERIODS                  // the bit's mark
        RCR    CUR                      // the bit into C, LSB first
        MOVL   21
        BTSC   SFR_STATUS_REG, SB_FLAG_C
        MOVL   64
        CALL   SPACE
        DECSZ  BITS
        JMP    BIT
        DECSZ  BYTES
        JMP    BYTE
        MOVL   1
        MOVA   SFR_DATA_EXCH
        MOVL   21
        CALL   PERIODS                  // the closing mark
        BC     SFR_PORT_IO, SB_PORT_OUT0
        CLRA
        MOVA   SFR_CTRL_RD
        BS     SFR_SYS_CFG, SB_INT_REQ
        JMP    WAIT
PIOC_END ir_nec
