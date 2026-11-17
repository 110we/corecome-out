/* 独立的 ELF 用户程序：会被 SYS_EXEC_PATH 加载（不是链接进内核的 blob）。
 * 链接地址必须是用户窗基址 0x10000000。 */
    .section .text
    .global _start
_start:
    adr x0, msg
    mov x8, #1
    svc #0x80
    mov x0, #0
    mov x8, #9
    svc #0x80
    b .

    .section .rodata
msg:
    .asciz "elf: hello from ELF\n"
