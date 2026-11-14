/* Core-owned section layout, shared by every variant (ADR-0003).
 * The MEMORY block (FLASH/RAM origin and length) is variant-owned and lives in
 * variants/<VARIANT>/<device>.ld, which INCLUDEs this file.
 */
/* Included from the per-SKU memory scripts (ch32v006_62k_8k.ld etc.). */

/* __stack_size: platform.txt's --defsym, from the Stack Size menu (boards.txt). */
PROVIDE( _stack_size = __stack_size );

/* For CH32RV.getSketchSize() / getFreeSketchSpace(): the flash this build is
 * linked into (the part the menu picked; the smallest of the series for ANY). */
PROVIDE( _ch32rv_flash_origin = ORIGIN(FLASH) );
PROVIDE( _ch32rv_flash_length = LENGTH(FLASH) );

SECTIONS
{
    /* Must be first: entry 0 is the reset jump, and mtvec's base has to be 0
     * because QingKe V2 drops the low bits of the address (see crt0_ch32.S). */
    .vector :
    {
        KEEP(*(.vector))
        . = ALIGN(4);
    } >FLASH

    .init :
    {
        . = ALIGN(4);
        KEEP(*(SORT_NONE(.init)))
        . = ALIGN(4);
    } >FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text*)
        *(.rodata*)
        *(.gnu.linkonce.t.*)
        . = ALIGN(4);
    } >FLASH

    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } >FLASH

    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.init_array .ctors))
        PROVIDE_HIDDEN(__init_array_end = .);
    } >FLASH

    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(.fini_array* .dtors*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } >FLASH

    /* Code the sketch asks to run from RAM (CH32RV_RAMFUNC, Arduino.h).
     * crt0_ch32.S copies it together with .data, so it must stay right before
     * .data in both RAM and flash (asserted below). Empty unless used.
     * Both start on 8 bytes in RAM and in flash: lld aligns an output
     * section's load address as well as its address, so a .data holding an
     * 8-byte object (a double, a uint64_t) moved 4 bytes in flash past the
     * address the copy started from, and every initialised variable came out
     * shifted (CH32V307, 2026-11-12: a delivery's function pointer read 0). */
    .highcode : ALIGN(8)
    {
        PROVIDE(_highcode_vma_start = .);
        *(.highcode)
        *(.highcode.*)
        . = ALIGN(8);
        PROVIDE(_highcode_vma_end = .);
    } >RAM AT>FLASH
    PROVIDE(_highcode_lma = LOADADDR(.highcode));

    .data : ALIGN(8)
    {
        *(.gnu.linkonce.r.*)
        *(.data*)
        *(.gnu.linkonce.d.*)
        . = ALIGN(8);
        PROVIDE( __global_pointer$ = . + 0x800 );
        *(.sdata*)
        *(.srodata*)
        . = ALIGN(4);
        PROVIDE( _edata = . );
    } >RAM AT>FLASH
    PROVIDE(_data_vma = ADDR(.data));
    PROVIDE(_data_lma = LOADADDR(.data));

    /* Kept through a reset that leaves RAM powered - a software or watchdog
     * reset, the restart deepSleep() ends with: startup neither copies nor
     * clears it. After a power-on it holds whatever the RAM held. */
    .noinit (NOLOAD) :
    {
        . = ALIGN(4);
        *(.noinit*)
        . = ALIGN(4);
    } >RAM

    .bss :
    {
        . = ALIGN(4);
        PROVIDE( _sbss = . );
        *(.sbss*)
        *(.bss*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(4);
        PROVIDE( _ebss = . );
    } >RAM

    /* Where the image ends in flash: ch32rv_flash.c refuses to erase or program below it. */
    PROVIDE( _ch32rv_image_end = LOADADDR(.data) + SIZEOF(.data) );

    PROVIDE( _end = _ebss );
    PROVIDE( end = . );

    /* NOLOAD: GNU ld makes a section of only symbols and a moved `.` NOBITS on
     * its own, lld makes it PROGBITS - a RAM-sized gap in the .bin. */
    .stack ORIGIN(RAM) + LENGTH(RAM) - __stack_size (NOLOAD) :
    {
        PROVIDE( _heap_end = . );
        . = ALIGN(4);
        PROVIDE( _susrstack = . );
        . = . + __stack_size;
        PROVIDE( _eusrstack = . );
    } >RAM

    /* Storage kept across resets (CH32RV_FLASH_STORE, ch32rv_flash.h), packed
     * against the end of FLASH. Only what something references survives
     * --gc-sections, so it is 0 B unless used; NOLOAD keeps it out of the
     * image. Overlapping code fails the link. */
    .ch32rv_store ORIGIN(FLASH) + LENGTH(FLASH) - SIZEOF(.ch32rv_store) (NOLOAD) :
    {
        PROVIDE( _ch32rv_store_start = . );
        *(.ch32rv_store*)
        PROVIDE( _ch32rv_store_end = . );
    } >FLASH
}

ASSERT(_data_lma - _highcode_lma == _data_vma - _highcode_vma_start,
       "crt0 copies .highcode and .data in one loop: they must sit alike in flash and RAM")
ASSERT(_vector_base == 0,
       "the interrupt vector table must start at the reset address")
