/* Altered from the release 3gx_crt0.s. Luma calls 0x07000100 on the owned
 * original startup thread with the saved game instructions pointer at [sp].
 * Preserve callee-saved registers and align the C-call stack explicitly. */
.syntax unified
.cpu mpcore
.fpu vfp
.arm
.section .crt0,"ax",%progbits
.balign 16
.global _start,__service_ptr
.type _start,%function
_start:
    b startup
__service_ptr:
    .word 0
startup:
    push {r4-r11,lr}
    mov r4,sp
    bic sp,sp,#7
    ldr r0,=__bss_start__
    ldr r1,=__bss_end__
    mov r2,#0
1:
    cmp r0,r1
    strlo r2,[r0],#4
    blo 1b
    ldr r0,[r4,#36]
    bl main
    mov sp,r4
    pop {r4-r11,pc}
.size _start,.-_start
.section .note.GNU-stack,"",%progbits
