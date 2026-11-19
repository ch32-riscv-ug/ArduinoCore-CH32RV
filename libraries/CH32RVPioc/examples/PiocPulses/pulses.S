#include <ch32rv_pioc.inc>

/* IO0 an output, low. On each command n from the host: n pulses on IO0 (high
 * for 4 cycles, low for 4), then 0x5A in cmd.out with the request set, and
 * the number of commands so far in d1. */
PIOC_BEGIN pulses
        DW     0x0000                      // word 0: reserved
START:  BC     SFR_PORT_IO, SB_PORT_OUT0
        MOVA1F 0x01                        // SFR_PORT_DIR: IO0 an output
        CLR    SFR_DATA_REG1
WAIT:   WAITB  WB_DATA_MW_SR_1             // the host wrote cmd.in
        MOV    SFR_CTRL_WR, A              // read it (clears the flag)
        MOVA   SFR_DATA_REG31
        INC    SFR_DATA_REG1
LOOP:   BS     SFR_PORT_IO, SB_PORT_OUT0
        NOP
        NOP
        NOP
        BC     SFR_PORT_IO, SB_PORT_OUT0
        NOP
        DECSZ  SFR_DATA_REG31
        JMP    LOOP
        MOVL   0x5A
        MOVA   SFR_CTRL_RD
        BS     SFR_SYS_CFG, SB_INT_REQ
        JMP    WAIT
PIOC_END pulses
