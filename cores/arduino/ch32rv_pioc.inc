/* PIOC (WCH RISC8B) programs in a sketch's or library's .S, assembled by the
 * core's own clang: every instruction is a macro that emits its 16-bit word,
 * so no other assembler is involved.
 *
 *   #include <ch32rv_pioc.inc>
 *
 *   PIOC_BEGIN blink
 *           DW    0x0000                 // word 0: reserved, never executed
 *   START:  MOVA1F 0x01                  // IO0 an output
 *   LOOP:   BS    SFR_PORT_IO, SB_PORT_OUT0
 *           BC    SFR_PORT_IO, SB_PORT_OUT0
 *           JMP   LOOP
 *   PIOC_END blink
 *
 * and in C/C++ `PIOC_IMAGE(blink)` (CH32RVPioc.h) names the image for the
 * loader. The mnemonics, their aliases, the `f,d` operands (d: A or 0 to A,
 * F, 1 or nothing to the register) and the SFR / bit names are WCH's - the
 * RISC8B instruction set manual (CHRISC8B, V2B) and the EVT's PIOC_INC.ASM -
 * so a vendor ASM moves over line by line. What differs: comments are // or
 * C's (the file goes through the C preprocessor; `;` separates statements and
 * `#` starts a directive), constants are `.equ NAME, value` (or #define), and
 * the program sits between PIOC_BEGIN and PIOC_END. ORG, DW and DB (RETL)
 * are macros too; ORG may only move forward and fills with NOP, as WASM53B.
 *
 * Checked when the image is assembled (an .error names the line's branch):
 * every immediate, register and bit field fits its encoding; JMP / CALL
 * targets are inside the 2048-word program; a conditional jump (JNZ / JZ /
 * JNC / JC) keeps the target's bit 10 equal to that of the next word, as the
 * core only replaces the low 10 bits; CMPZ keeps bits 10:8 (it replaces the
 * low 8); the image is at most 4096 bytes (the PIOC's program SRAM). The
 * branches are checked at PIOC_END, when every label is known.
 *
 * Only PIOC code belongs in a file that includes this: the macros (NOP, AND,
 * RET, CALL ...) take the place of the RISC-V instructions of the same names.
 * The words follow the CH32X035 PIOC's decode (0x23xx is MOVA1F, not a 9-bit
 * MOVIP), so MOVIP and MOVIA take 8-bit operands here. */

/* ------------------------------------------------------------ SFRs */
.equ SFR_INDIR_PORT,  0x00
.equ SFR_INDIR_PORT2, 0x01
.equ SFR_PRG_COUNT,   0x02
.equ SFR_STATUS_REG,  0x03
.equ SFR_INDIR_ADDR,  0x04
.equ SFR_TMR0_COUNT,  0x05
.equ SFR_TIMER_CTRL,  0x06
.equ SFR_TMR0_INIT,   0x07
.equ SFR_BIT_CYCLE,   0x08
.equ SFR_INDIR_ADDR2, 0x09
.equ SFR_PORT_DIR,    0x0A
.equ SFR_PORT_IO,     0x0B
.equ SFR_BIT_CONFIG,  0x0C
.equ SFR_SYS_CFG,     0x1C
.equ SFR_CTRL_RD,     0x1D
.equ SFR_CTRL_WR,     0x1E
.equ SFR_DATA_EXCH,   0x1F
.equ SFR_DATA_REG0,   0x20
.equ SFR_DATA_REG1,   0x21
.equ SFR_DATA_REG2,   0x22
.equ SFR_DATA_REG3,   0x23
.equ SFR_DATA_REG4,   0x24
.equ SFR_DATA_REG5,   0x25
.equ SFR_DATA_REG6,   0x26
.equ SFR_DATA_REG7,   0x27
.equ SFR_DATA_REG8,   0x28
.equ SFR_DATA_REG9,   0x29
.equ SFR_DATA_REG10,  0x2A
.equ SFR_DATA_REG11,  0x2B
.equ SFR_DATA_REG12,  0x2C
.equ SFR_DATA_REG13,  0x2D
.equ SFR_DATA_REG14,  0x2E
.equ SFR_DATA_REG15,  0x2F
.equ SFR_DATA_REG16,  0x30
.equ SFR_DATA_REG17,  0x31
.equ SFR_DATA_REG18,  0x32
.equ SFR_DATA_REG19,  0x33
.equ SFR_DATA_REG20,  0x34
.equ SFR_DATA_REG21,  0x35
.equ SFR_DATA_REG22,  0x36
.equ SFR_DATA_REG23,  0x37
.equ SFR_DATA_REG24,  0x38
.equ SFR_DATA_REG25,  0x39
.equ SFR_DATA_REG26,  0x3A
.equ SFR_DATA_REG27,  0x3B
.equ SFR_DATA_REG28,  0x3C
.equ SFR_DATA_REG29,  0x3D
.equ SFR_DATA_REG30,  0x3E
.equ SFR_DATA_REG31,  0x3F

/* SFR_STATUS_REG (WCH's PIOC_INC.ASM; the PIOC manual swaps bits 5 and 4) */
.equ SB_EN_TOUT_RST,  5
.equ SB_STACK_USED,   4
.equ SB_GP_BIT_Y,     3
.equ SB_FLAG_Z,       2
.equ SB_GP_BIT_X,     1
.equ SB_FLAG_C,       0
/* SFR_TIMER_CTRL */
.equ SB_EN_LEVEL1,    7
.equ SB_EN_LEVEL0,    6
.equ SB_TMR0_ENABLE,  5
.equ SB_TMR0_OUT_EN,  4
.equ SB_TMR0_MODE,    3
.equ SB_TMR0_FREQ2,   2
.equ SB_TMR0_FREQ1,   1
.equ SB_TMR0_FREQ0,   0
/* SFR_BIT_CYCLE */
.equ SB_BIT_TX_O0,    7
.equ SB_BIT_CYCLE_6,  6
.equ SB_BIT_CYCLE_5,  5
.equ SB_BIT_CYCLE_4,  4
.equ SB_BIT_CYCLE_3,  3
.equ SB_BIT_CYCLE_2,  2
.equ SB_BIT_CYCLE_1,  1
.equ SB_BIT_CYCLE_0,  0
/* SFR_PORT_DIR */
.equ SB_PORT_MOD3,    7
.equ SB_PORT_MOD2,    6
.equ SB_PORT_MOD1,    5
.equ SB_PORT_MOD0,    4
.equ SB_PORT_PU1,     3
.equ SB_PORT_PU0,     2
.equ SB_PORT_DIR1,    1
.equ SB_PORT_DIR0,    0
/* SFR_PORT_IO */
.equ SB_PORT_IN_XOR,  7
.equ SB_BIT_RX_I0,    6
.equ SB_PORT_IN1,     5
.equ SB_PORT_IN0,     4
.equ SB_PORT_XOR1,    3
.equ SB_PORT_XOR0,    2
.equ SB_PORT_OUT1,    1
.equ SB_PORT_OUT0,    0
/* SFR_BIT_CONFIG */
.equ SB_BIT_TX_EN,    7
.equ SB_BIT_CODE_MOD, 6
.equ SB_PORT_IN_EDGE, 5
.equ SB_BIT_CYC_TAIL, 4
.equ SB_BIT_CYC_CNT6, 3
.equ SB_BIT_CYC_CNT5, 2
.equ SB_BIT_CYC_CNT4, 1
.equ SB_BIT_CYC_CNT3, 0
/* SFR_SYS_CFG */
.equ SB_INT_REQ,      7
.equ SB_DATA_SW_MR,   6
.equ SB_DATA_MW_SR,   5
.equ SB_MST_CFG_B4,   4
.equ SB_MST_IO_EN1,   3
.equ SB_MST_IO_EN0,   2
.equ SB_MST_RESET,    1
.equ SB_MST_CLK_GATE, 0
/* BCTC */
.equ BI_C_XOR_IN0,    0
/* BP1F / BP2F / BG1F / BG2F */
.equ BIO_FLAG_C,      0
/* BCTC / BG1F / BG2F */
.equ BI_BIT_RX_I0,    1
.equ BI_PORT_IN0,     2
.equ BI_PORT_IN1,     3
/* BP1F / BP2F */
.equ BO_BIT_TX_O0,    1
.equ BO_PORT_OUT0,    2
.equ BO_PORT_OUT1,    3
/* WAITB */
.equ WB_DATA_SW_MR_0,   0
.equ WB_BIT_CYC_TAIL_1, 1
.equ WB_PORT_I0_FALL,   2
.equ WB_PORT_I0_RISE,   3
.equ WB_DATA_MW_SR_1,   4
.equ WB_PORT_XOR1_1,    5
.equ WB_PORT_XOR0_0,    6
.equ WB_PORT_XOR0_1,    7

/* ------------------------------------------------------------ the image */
.set __pioc_nbr, 0          /* branches so far in this file */
.set __pioc_open, 0

.macro PIOC_BEGIN name
  .if __pioc_open
    .error "PIOC_BEGIN \name inside another image (PIOC_END first)"
  .endif
  .set __pioc_open, 1
  .set __pioc_first, __pioc_nbr
  .section .rodata.pioc.\name,"a",@progbits
  .option push
  .option norelax           /* label differences stay constants */
  .p2align 2
  .global \name
  .global __pioc_image_\name
  .type \name, @object
\name:
__pioc_image_\name:
  /* __PIOC_OFF sym: sym = the bytes from the image's start to here. A
   * macro of this image's (its name written in), not an alias of the start:
   * the assembler folds a difference of two real labels in one run of words
   * at once, which the checks below need. */
  .macro __PIOC_OFF sym
    .set \sym, . - \name
  .endm
.endm

.macro PIOC_END name
  .if !__pioc_open
    .error "PIOC_END \name without PIOC_BEGIN"
  .endif
  __PIOC_OFF __pioc_size
  .purgem __PIOC_OFF
  .set __pioc_open, 0
  .if __pioc_size > 4096
    .error "PIOC image \name is larger than the PIOC's 4096-byte program SRAM"
  .endif
  .size \name, __pioc_size
  .set __pioc_i, __pioc_first
  .rept __pioc_nbr - __pioc_first
    .set __pioc_i, __pioc_i + 1
    .altmacro
    __PIOC_CHECK %__pioc_i, \name
    .noaltmacro
  .endr
  /* The size, for C (PIOC_IMAGE()): a word of its own, so the image's
   * section holds the image alone. */
  .section .rodata.pioc_size.\name,"a",@progbits
  .p2align 2
  .global \name\()_size
  .type \name\()_size, @object
\name\()_size:
  .word __pioc_size
  .size \name\()_size, 4
  .option pop
  .text
.endm

/* ------------------------------------------------------------ helpers */
.macro __PIOC_W v
  .if !__pioc_open
    .error "PIOC instruction outside PIOC_BEGIN / PIOC_END"
  .endif
  .short (\v) & 0xffff
.endm

.macro __PIOC_FIT v, max, what
  .if ((\v) < 0) || ((\v) > (\max))
    .error "PIOC \what out of range (0 .. \max)"
  .endif
.endm

/* d: A / a / 0 -> 0 (to A), F / f / 1 / nothing -> 1 (to the register) */
.macro __PIOC_D d
  .set __pioc_d, 1
  .ifc \d,A
    .set __pioc_d, 0
  .endif
  .ifc \d,a
    .set __pioc_d, 0
  .endif
  .ifc \d,0
    .set __pioc_d, 0
  .endif
  .ifnc \d,A
  .ifnc \d,a
  .ifnc \d,0
  .ifnc \d,F
  .ifnc \d,f
  .ifnc \d,1
  .ifnb \d
    .error "PIOC destination must be A, F, 0, 1 or nothing"
  .endif
  .endif
  .endif
  .endif
  .endif
  .endif
  .endif
.endm

/* A branch: the word, and a label and the distance to the target for the
 * check at PIOC_END (forward targets are not known here). shift: the bits
 * the core keeps from the next word's address (10, 8), 0 for JMP / CALL. */
.macro __PIOC_BRANCH word, mask, shift, t
  .set __pioc_nbr, __pioc_nbr + 1
  .altmacro
  __PIOC_BRANCH_AT %__pioc_nbr, \word, \mask, \shift, \t
  .noaltmacro
.endm

.macro __PIOC_BRANCH_AT n, word, mask, shift, t
.L__pioc_pc_\n:
  __PIOC_OFF .L__pioc_at_\n
  .set .L__pioc_off_\n, (\t) - .L__pioc_pc_\n
  .set .L__pioc_shift_\n, \shift
  __PIOC_W (\word) | ((((\t) - .L__pioc_pc_\n) / 2 + .L__pioc_at_\n / 2) & (\mask))
.endm

.macro __PIOC_CHECK n, name
  .set __pioc_w, .L__pioc_at_\n / 2
  .set __pioc_t, __pioc_w + .L__pioc_off_\n / 2
  .if (__pioc_t < 0) || (__pioc_t > 0x7ff)
    .error "PIOC branch \n of \name: target outside the 2048-word program"
  .endif
  .if .L__pioc_shift_\n
    .if (__pioc_t >> .L__pioc_shift_\n) != (((__pioc_w + 1) & 0x7ff) >> .L__pioc_shift_\n)
      .error "PIOC branch \n of \name: target out of reach (a conditional jump keeps bit 10, CMPZ bits 10:8 of the next word's address)"
    .endif
  .endif
.endm

/* ------------------------------------------------------------ directives */
.macro DW v
  __PIOC_W \v
.endm

.macro ORG addr
  .if (\addr) > 0x7ff
    .error "PIOC ORG \addr is past the 2048-word program"
  .endif
  __PIOC_OFF __pioc_here
  .if (\addr) * 2 < __pioc_here
    .error "PIOC ORG \addr goes back"
  .endif
  /* NOP (0) up to there - words, not .org, which would part the run of
   * words and leave the label differences unknown until the end */
  .rept ((\addr) * 2 - __pioc_here) / 2
    .short 0
  .endr
.endm

/* ------------------------------------------------------------ control */
.macro NOP
  __PIOC_W 0x0000
.endm
.macro CLRWDT
  __PIOC_W 0x0008
.endm
.macro WDT
  __PIOC_W 0x0008
.endm
.macro SLEEP
  __PIOC_W 0x000C
.endm
.macro HALT
  __PIOC_W 0x000C
.endm
.macro SLEEPX k
  __PIOC_FIT \k, 3, "SLEEPX mode"
  __PIOC_W 0x000C | (\k)
.endm
.macro WAITB b
  __PIOC_FIT \b, 7, "WAITB bit"
  __PIOC_W 0x0010 | (\b)
.endm
.macro WAITRD
  __PIOC_W 0x0010
.endm
.macro WAITWR
  __PIOC_W 0x0014
.endm
.macro WAITSPI
  __PIOC_W 0x0014
.endm
.macro RDCODE
  __PIOC_W 0x0018
.endm
.macro RCODE k
  __PIOC_FIT \k, 3, "RCODE space"
  __PIOC_W 0x0018 | (\k)
.endm
.macro WRCODE
  __PIOC_W 0x001C
.endm
.macro EXEC k
  __PIOC_FIT \k, 3, "EXEC parameter"
  __PIOC_W 0x001C | (\k)
.endm
.macro BCTC a
  __PIOC_FIT \a, 3, "BCTC bit"
  __PIOC_W 0x001C | (\a)
.endm
.macro PUSHAS
  __PIOC_W 0x0020
.endm
.macro PUSH
  __PIOC_W 0x0020
.endm
.macro POPAS
  __PIOC_W 0x0024
.endm
.macro POP
  __PIOC_W 0x0024
.endm
.macro PUSHA2
  __PIOC_W 0x0028
.endm
.macro POPA2
  __PIOC_W 0x002C
.endm
.macro RET
  __PIOC_W 0x0030
.endm
.macro RETURN
  __PIOC_W 0x0030
.endm
.macro RETZ
  __PIOC_W 0x0034
.endm
.macro RETOK
  __PIOC_W 0x0034
.endm
.macro RETIE
  __PIOC_W 0x0038
.endm
.macro RETI
  __PIOC_W 0x0038
.endm

/* ------------------------------------------------------------ byte operations */
.macro CLRA
  __PIOC_W 0x0004
.endm
.macro CLR f
  __PIOC_FIT \f, 0xff, "register"
  __PIOC_W 0x0100 | (\f)
.endm
.macro CLRF f
  CLR \f
.endm
.macro MOVA F
  __PIOC_FIT \F, 0x1ff, "register"
  __PIOC_W 0x1000 | (\F)
.endm
.macro MOV F, d
  __PIOC_FIT \F, 0x1ff, "register"
  __PIOC_D \d
  __PIOC_W (__pioc_d << 12) | 0x0200 | (\F)
.endm
.macro MOVF F, d
  MOV \F, \d
.endm

.macro __PIOC_BYTE op, f, d
  __PIOC_FIT \f, 0xff, "register"
  __PIOC_D \d
  __PIOC_W (__pioc_d << 12) | ((\op) << 8) | (\f)
.endm
.macro INC f, d
  __PIOC_BYTE 0x4, \f, \d
.endm
.macro INCF f, d
  __PIOC_BYTE 0x4, \f, \d
.endm
.macro DEC f, d
  __PIOC_BYTE 0x5, \f, \d
.endm
.macro DECF f, d
  __PIOC_BYTE 0x5, \f, \d
.endm
.macro INCSZ f, d
  __PIOC_BYTE 0x6, \f, \d
.endm
.macro INCFSZ f, d
  __PIOC_BYTE 0x6, \f, \d
.endm
.macro DECSZ f, d
  __PIOC_BYTE 0x7, \f, \d
.endm
.macro DECFSZ f, d
  __PIOC_BYTE 0x7, \f, \d
.endm
.macro SWAP f, d
  __PIOC_BYTE 0x8, \f, \d
.endm
.macro SWAPF f, d
  __PIOC_BYTE 0x8, \f, \d
.endm
.macro AND f, d
  __PIOC_BYTE 0x9, \f, \d
.endm
.macro ANDF f, d
  __PIOC_BYTE 0x9, \f, \d
.endm
.macro IOR f, d
  __PIOC_BYTE 0xA, \f, \d
.endm
.macro IORF f, d
  __PIOC_BYTE 0xA, \f, \d
.endm
.macro XOR f, d
  __PIOC_BYTE 0xB, \f, \d
.endm
.macro XORF f, d
  __PIOC_BYTE 0xB, \f, \d
.endm
.macro ADD f, d
  __PIOC_BYTE 0xC, \f, \d
.endm
.macro ADDF f, d
  __PIOC_BYTE 0xC, \f, \d
.endm
.macro SUB f, d
  __PIOC_BYTE 0xD, \f, \d
.endm
.macro SUBF f, d
  __PIOC_BYTE 0xD, \f, \d
.endm
.macro RCL f, d
  __PIOC_BYTE 0xE, \f, \d
.endm
.macro RCLF f, d
  __PIOC_BYTE 0xE, \f, \d
.endm
.macro RLF f, d
  __PIOC_BYTE 0xE, \f, \d
.endm
.macro RCR f, d
  __PIOC_BYTE 0xF, \f, \d
.endm
.macro RCRF f, d
  __PIOC_BYTE 0xF, \f, \d
.endm
.macro RRF f, d
  __PIOC_BYTE 0xF, \f, \d
.endm

/* ------------------------------------------------------------ constants */
.macro __PIOC_LIT op, k
  __PIOC_FIT \k, 0xff, "constant"
  __PIOC_W ((\op) << 8) | (\k)
.endm
.macro RETL k
  __PIOC_LIT 0x20, \k
.endm
.macro DB k
  __PIOC_LIT 0x20, \k
.endm
.macro RETLN k
  __PIOC_LIT 0x21, \k
.endm
.macro RETER k
  __PIOC_LIT 0x21, \k
.endm
/* 8 bits on the PIOC: 0x23xx is MOVA1F, 0x25xx..0x27xx MOVA2F / MOVA2P / MOVA1P */
.macro MOVIP k
  __PIOC_LIT 0x22, \k
.endm
.macro MOVA1F k
  __PIOC_LIT 0x23, \k
.endm
.macro MOVIA k
  __PIOC_LIT 0x24, \k
.endm
.macro MOVA2F k
  __PIOC_LIT 0x25, \k
.endm
.macro MOVA2P k
  __PIOC_LIT 0x26, \k
.endm
.macro MOVA1P k
  __PIOC_LIT 0x27, \k
.endm
.macro MOVL k
  __PIOC_LIT 0x28, \k
.endm
.macro ANDL k
  __PIOC_LIT 0x29, \k
.endm
.macro IORL k
  __PIOC_LIT 0x2A, \k
.endm
.macro ORL k
  __PIOC_LIT 0x2A, \k
.endm
.macro XORL k
  __PIOC_LIT 0x2B, \k
.endm
.macro ADDL k
  __PIOC_LIT 0x2C, \k
.endm
.macro SUBL k
  __PIOC_LIT 0x2D, \k
.endm
.macro CMPLN k
  __PIOC_LIT 0x2E, \k
.endm
.macro CMPL k
  __PIOC_LIT 0x2F, \k
.endm

/* ------------------------------------------------------------ bits */
.macro __PIOC_BIT op, f, b
  __PIOC_FIT \f, 0xff, "register"
  __PIOC_FIT \b, 7, "bit"
  __PIOC_W (\op) | ((\b) << 8) | (\f)
.endm
.macro BC f, b
  __PIOC_BIT 0x4000, \f, \b
.endm
.macro BCF f, b
  __PIOC_BIT 0x4000, \f, \b
.endm
.macro BS f, b
  __PIOC_BIT 0x4800, \f, \b
.endm
.macro BSF f, b
  __PIOC_BIT 0x4800, \f, \b
.endm
.macro BTSC f, b
  __PIOC_BIT 0x5000, \f, \b
.endm
.macro BTFSC f, b
  __PIOC_BIT 0x5000, \f, \b
.endm
.macro BTSS f, b
  __PIOC_BIT 0x5800, \f, \b
.endm
.macro BTFSS f, b
  __PIOC_BIT 0x5800, \f, \b
.endm

.macro __PIOC_XFER op, a, b
  __PIOC_FIT \a, 3, "bit selection"
  __PIOC_FIT \b, 7, "bit"
  __PIOC_W (\op) | ((\a) << 3) | (\b)
.endm
.macro BP1F a, b
  __PIOC_XFER 0x0080, \a, \b
.endm
.macro BP2F a, b
  __PIOC_XFER 0x00A0, \a, \b
.endm
.macro BG1F a, b
  __PIOC_XFER 0x00C0, \a, \b
.endm
.macro BG2F a, b
  __PIOC_XFER 0x00E0, \a, \b
.endm

/* ------------------------------------------------------------ branches */
.macro JMP k
  __PIOC_BRANCH 0x6000, 0xfff, 0, \k
.endm
.macro GOTO k
  __PIOC_BRANCH 0x6000, 0xfff, 0, \k
.endm
.macro CALL k
  __PIOC_BRANCH 0x7000, 0xfff, 0, \k
.endm
.macro JNZ k
  __PIOC_BRANCH 0x3000, 0x3ff, 10, \k
.endm
.macro JZ k
  __PIOC_BRANCH 0x3400, 0x3ff, 10, \k
.endm
.macro JNC k
  __PIOC_BRANCH 0x3800, 0x3ff, 10, \k
.endm
.macro JC k
  __PIOC_BRANCH 0x3C00, 0x3ff, 10, \k
.endm
.macro CMPZ k, t
  __PIOC_FIT \k, 0x7f, "CMPZ constant"
  __PIOC_BRANCH 0x8000 | ((\k) << 8), 0xff, 8, \t
.endm
