/* The CH32H41x V5F layout (CPU Core menu: V5F; CH32RV_RUN_V5F), found ahead of
 * the core's own sections_loadcode.ld through build.ld_layout_flags.
 *
 * The same image as sections_loadcode.ld, except where the code goes: into
 * the V5F's ITCM first (zero-wait, its own bus), and what does not fit there
 * into the shared SRAM image (ld --enable-non-contiguous-regions takes each
 * input section to the first place it fits). Measured on CH32H417
 * (2026-11-11): 64 KB of code ran 2.5 times slower from the shared SRAM than
 * from ITCM once it no longer fitted the 32 KB instruction cache; code that
 * fits the cache runs as fast from either. Data, the stack and every DMA
 * buffer stay in the shared SRAM, the only memory the USB DMA reaches.
 * crt0 copies the ITCM part before it leaves flash. The variant's MEMORY
 * names ITCM.
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

    .itcm :
    {
        PROVIDE(_itcm_vma = .);
        *(.text*)
        *(.highcode)
        *(.highcode.*)
        . = ALIGN(4);
        PROVIDE(_itcm_end = .);
    } >ITCM AT>FLASH
    PROVIDE(_itcm_lma = LOADADDR(.itcm));

    .image :
    {
        PROVIDE(_image_vma = .);
        KEEP(*(.vector))
        . = ALIGN(4);
        KEEP(*(SORT_NONE(.init)))
        *(.text*)                      /* what ITCM could not take */
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
    PROVIDE( _ch32rv_image_end = MAX(LOADADDR(.image) + SIZEOF(.image), LOADADDR(.itcm) + SIZEOF(.itcm)) );

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

ASSERT(_start == ORIGIN(FLASH), "_start must be at the reset address")
ASSERT(_image_vma == ORIGIN(RAM), "the vector table must start the RAM image")
