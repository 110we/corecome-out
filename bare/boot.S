/* boot.S —— 上电后第一段代码。
 * 只留 CPU0、立栈、清 .bss、装向量表，然后进 C。
 * x0 进来时是 QEMU 传的 DTB 指针，C 后面要用，先寄存在 x19。
 */

    .section .text.boot
    .global _start
_start:
    mrs  x1, mpidr_el1
    and  x1, x1, #0xff
    cbnz x1, park

    mov  x19, x0            /* dtb */

    ldr  x0, =__stack_top
    mov  sp, x0

    ldr  x0, =__bss_start
    ldr  x1, =__bss_end
clear_bss:
    cmp  x0, x1
    b.hs bss_done
    str  xzr, [x0], #8
    b    clear_bss
bss_done:

    /* 向量表必须在任何可能异常的指令之前装好 */
    ldr  x0, =vectors
    msr  vbar_el1, x0
    isb

    mov  x0, x19
    bl   kmain

park:
    wfe
    b    park

    .ltorg
