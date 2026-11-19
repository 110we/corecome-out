ENTRY(_start)

PHDRS
{
    text   PT_LOAD FLAGS(5); /* R-X */
    rodata PT_LOAD FLAGS(4); /* R-- */
    data   PT_LOAD FLAGS(6); /* RW- */
}

SECTIONS
{
    . = 0x40080000;
    __kernel_start = .;

    .text : ALIGN(4096) {
        __kernel_text_start = .;
        KEEP(*(.text.boot))
        *(.text .text.*)
        . = ALIGN(4096);
        __kernel_text_end = .;
    } :text

    .rodata : ALIGN(4096) {
        __kernel_rodata_start = .;
        *(.rodata .rodata.*)
        . = ALIGN(4096);
        __kernel_rodata_end = .;
    } :rodata

    .data : ALIGN(4096) {
        __kernel_data_start = .;
        *(.data .data.*)
    } :data

    .bss (NOLOAD) : ALIGN(4096) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4096);
        __bss_end = .;
    } :data

    . = ALIGN(4096);
    . += 0x10000;
    __stack_top = .;
    __kernel_data_end = .;

    __kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
