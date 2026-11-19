#include <ch32rv_pioc.inc>

/* An SPI transmitter, mode 0, MSB first: SCK on IO0 (idle low), MOSI on IO1, its bytes in the shared registers.
 * CS is the sketch's (a GPIO): the PIOC has two pads. The PIOC keeps running between commands, so SCK stays low.
 *
 * Each command: d0 the bytes to send (1 .. 24) from d8 .. d31, d1 the half period as the turns of the wait below
 * (1 .. 256, 0 for 256; the host works it out from the PIOC's clock). Every bit: MOSI set while SCK is low, a half
 * period, SCK high (the receiver samples), a half period, SCK low. Then cmd.out 0 with the request set. */
.equ COUNT,   SFR_DATA_REG0
.equ T_HALF,  SFR_DATA_REG1
.equ TURN,    SFR_DATA_REG3
.equ FIRST,   SFR_DATA_REG8             // the bytes: d8 .. d31

PIOC_BEGIN spi_tx
        DW     0x0000                   // word 0: reserved
        JMP    START
        ORG    0x0008
// half a period: T_HALF turns of a 3-cycle loop and the call around it
HALF:   MOV    T_HALF, A
        MOVA   TURN
H1:     DECSZ  TURN
        JMP    H1
        RET
// one bit of SFR_DATA_EXCH onto MOSI, then the clock pulse
.macro  BIT n
        BP2F   BO_PORT_OUT1, \n         // MOSI, SCK low
        CALL   HALF
        BS     SFR_PORT_IO, SB_PORT_OUT0   // SCK rises: the receiver samples
        CALL   HALF
        BC     SFR_PORT_IO, SB_PORT_OUT0   // SCK falls
.endm
SEND:   BIT    7
        BIT    6
        BIT    5
        BIT    4
        BIT    3
        BIT    2
        BIT    1
        BIT    0
        RET
START:  BC     SFR_PORT_IO, SB_PORT_OUT0   // SCK idle low
        MOVA1F 0x03                     // IO0 and IO1 outputs
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
PIOC_END spi_tx
