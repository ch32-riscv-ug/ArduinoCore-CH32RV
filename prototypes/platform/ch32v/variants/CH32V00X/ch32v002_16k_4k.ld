/* CH32V002x4: FLASH 16K, SRAM 4K (datasheet facts; to be generated from ch32-device-data) */
ENTRY( _start )

MEMORY
{
    FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 16K
    RAM (xrw)  : ORIGIN = 0x20000000, LENGTH = 4K
}

INCLUDE sections.ld
