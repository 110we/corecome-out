/* linker.ld —— 告诉链接器：代码放哪、bss 从哪到哪、栈立在哪。
 *
 * RAM 从 0x40000000 开始，但 QEMU 把 arm64 Image 固定加载到 +0x80000，
 * 也就是 0x40080000（Linux arm64 的惯例）。链接基址必须跟它一致，
 * 否则 -kernel 一个 ELF 能跑、换成 Image 就整段偏移 512K。
 */

ENTRY(_start)

/* 代码只读可执行，数据可读写——别给内核留一个 RWX 段 */
PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x40080000;

    .text : {
        KEEP(*(.text.boot))   /* 必须在最前：入口不能跑到别处去 */
        *(.text .text.*)
    } :text

    .rodata : {
        *(.rodata .rodata.*)
    } :text

    .data : {
        *(.data .data.*)
    } :data

    __bss_start = .;
    .bss (NOLOAD) : {
        *(.bss .bss.*)
        *(COMMON)
    } :data
    . = ALIGN(8);
    __bss_end = .;

    . = ALIGN(16);
    __stack_bottom = .;
    . += 0x20000;             /* 128 KiB 初始栈 */
    __stack_top = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
    }
}
