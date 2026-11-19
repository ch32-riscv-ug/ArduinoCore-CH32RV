#include <ch32rv_pioc.inc>

/* The bench's PIOC program (tests/instrumented/pioc). IO0 an output, low.
 * Each command n: n pulses on IO0, high and low each for d0 turns of a
 * 3-cycle loop (so the width follows the PIOC's clock, d0 picks it), then
 * 0x5A in cmd.out with the request set; d1 counts the commands. Uses ORG,
 * CALL / RET, DECSZ and the wait on the host's write. */
.equ DELAY,   SFR_DATA_REG0
.equ SEEN,    SFR_DATA_REG1
.equ TURNS,   SFR_DATA_REG30
.equ PULSES,  SFR_DATA_REG31

PIOC_BEGIN bench_pulses
        DW     0x0000                   // word 0: reserved
        JMP    START
        ORG    0x0008
HALF:   MOV    DELAY, A                 // d0 turns of DECSZ + JMP
        MOVA   TURNS
HALF_1: DECSZ  TURNS
        JMP    HALF_1
        RET
START:  BC     SFR_PORT_IO, SB_PORT_OUT0
        MOVA1F 0x01                     // IO0 an output
        CLR    SEEN
WAIT:   WAITB  WB_DATA_MW_SR_1
        MOV    SFR_CTRL_WR, A
        MOVA   PULSES
        INC    SEEN
PULSE:  BS     SFR_PORT_IO, SB_PORT_OUT0
        CALL   HALF
        BC     SFR_PORT_IO, SB_PORT_OUT0
        CALL   HALF
        DECSZ  PULSES
        JMP    PULSE
        MOVL   0x5A
        MOVA   SFR_CTRL_RD
        BS     SFR_SYS_CFG, SB_INT_REQ
        JMP    WAIT
PIOC_END bench_pulses
