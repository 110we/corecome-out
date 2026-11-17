/* 独立 ELF 用户程序。本内核自定义约定（不是 Linux ABI）：
 *   x0 = argc，x1 = argv（argv[0] 是参数字符串指针）
 */
    .section .text
    .global _start
_start:
    mov x19, x0                 /* argc */
    mov x20, x1                 /* argv */

    mov x0, #1
    adr x1, pre
    mov x2, #9
    mov x8, #4
    svc #0x80                   /* write(1, "elf-arg: ", 9) */

    cbz x19, done
    cbz x20, done
    ldr x5, [x20]               /* argv[0] */
    cbz x5, done
    mov x6, x5
1:
    ldrb w7, [x6]
    cbz w7, 2f
    add x6, x6, #1
    b 1b
2:
    sub x2, x6, x5
    mov x0, #1
    mov x1, x5
    mov x8, #4
    svc #0x80                   /* write(1, argv[0], len) */

done:
    mov x0, #1
    adr x1, nl
    mov x2, #1
    mov x8, #4
    svc #0x80

    mov x0, #0
    mov x8, #9
    svc #0x80
    b .

    .section .rodata
pre:
    .asciz "elf-arg: "
nl:
    .asciz "\n"
