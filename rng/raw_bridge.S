.syntax unified
.arch armv6k
.fpu vfp
.arm
.section .text.raw_alias,"ax",%progbits
.balign 4096
.global ch_raw_bridge
.type ch_raw_bridge,%function
ch_raw_bridge:
    /* The scheduler site repeats inside the original engine loop. Only its
       first call after a marker needs the observer. Later calls preserve the
       complete caller state and chain directly without C, SVC or VFP work. */
    push {r0,r1}
    mrs r0,cpsr
    ldr r1,.Lscheduler_lr
    cmp lr,r1
    bne .Lfull_route
    ldr r1,.Lpending_slot
    ldr r1,[r1]
    cmp r1,#0
    beq .Lfull_route
    ldr r1,[r1]
    cmp r1,#0
    bne .Lfull_route
    msr cpsr_fs,r0
    pop {r0,r1}
    ldr pc,.Lscheduler_target
.Lfull_route:
    msr cpsr_fs,r0
    pop {r0,r1}
    sub sp,sp,#208
    stmia sp,{r0-r12,lr}
    add r0,sp,#208
    str r0,[sp,#56]
    mrs r0,cpsr
    str r0,[sp,#60]
    vmrs r0,fpscr
    str r0,[sp,#64]
    mov r0,#0
    str r0,[sp,#68]
    add r0,sp,#72
    vstmia r0,{d0-d15}
    mov r0,sp
    bic sp,sp,#7
    sub sp,sp,#8
    str r0,[sp]
    ldr r12,.Lslot
    ldr r12,[r12]
    blx r12
    ldr r4,[sp]
    str r0,[r4,#200]
    mov sp,r4
    add r0,sp,#72
    vldmia r0,{d0-d15}
    ldr r0,[sp,#64]
    vmsr fpscr,r0
    ldr r0,[sp,#60]
    msr cpsr_fs,r0
    ldr r0,[sp,#200]
    str r0,[sp,#204]
    ldmia sp,{r0-r12,lr}
    ldr sp,[sp,#56]
    ldr pc,[sp,#-4]
.Lslot: .word ch_raw_callback
.Lpending_slot: .word ch_raw_scheduler_pending
.Lscheduler_lr: .word 0x1a8340
.Lscheduler_target: .word 0x1aae60
.global ch_manual_alias_probe
.type ch_manual_alias_probe,%function
ch_manual_alias_probe:
    eor r0,r0,#0x6100
    bx lr
