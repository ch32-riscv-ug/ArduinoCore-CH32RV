/* Core-owned section layout for the families that run from RAM
 * (CH32RV_LOADCODE: CH32H417). The variant's MEMORY block names FLASH, where
 * the image is stored, and RAM, where all of it runs.
 *
 * Only the reset jump and the copy loop (crt0_ch32.S, section .reset) run
 * from flash. Everything else - the vector table, code, read-only data, the
 * constructor tables and the initialised data - is one image linked at the
 * start of RAM and stored right after .reset in flash; crt0 copies it in one
 * loop, then clears .bss. The vector table is the first thing in RAM, so
 * mtvec's base is ORIGIN(RAM) (QingKe V3F keeps the full address; the
 * base-0 rule in sections.ld is for QingKe V2).
 */

__stack_size = DEFINED(__stack_size) ? __stack_size : 2048;
PROVIDE( _stack_size = __stack_size );

PROVIDE( _ch32rv_flash_origin = ORIGIN(FLASH) );
PROVIDE( _ch32rv_flash_length = LENGTH(FLASH) );

SECTIONS
{
    /* The reset address: _start (j handle_reset) and the copy. */
    .reset :
    {
        KEEP(*(.reset))
        . = ALIGN(4);
    } >FLASH

    .image :
    {
        PROVIDE(_image_vma = .);
        KEEP(*(.vector))
        . = ALIGN(4);
        KEEP(*(SORT_NONE(.init)))
        *(.text*)
        *(.rodata*)
        *(.gnu.linkonce.t.*)
        *(.highcode)
        *(.highcode.*)
        . = ALIGN(4);
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.init_array .ctors))
        PROVIDE_HIDDEN(__init_array_end = .);
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(.fini_array* .dtors*))
        PROVIDE_HIDDEN(__fini_array_end = .);
        . = ALIGN(4);
        PROVIDE(_data_vma = .);
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
    PROVIDE(_image_lma = LOADADDR(.image));

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

    PROVIDE( _end = _ebss );
    PROVIDE( end = . );

    .stack ORIGIN(RAM) + LENGTH(RAM) - __stack_size :
    {
        PROVIDE( _heap_end = . );
        . = ALIGN(4);
        PROVIDE( _susrstack = . );
        . = . + __stack_size;
        PROVIDE( _eusrstack = . );
    } >RAM
}

ASSERT(_start == ORIGIN(FLASH), "_start must be at the reset address")
ASSERT(_image_vma == ORIGIN(RAM), "the vector table must start the RAM image")
