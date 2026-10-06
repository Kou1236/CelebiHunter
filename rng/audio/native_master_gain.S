.syntax unified
.arch armv6k
.fpu vfp
.arm
.text
.balign 4
.global raw_audio_native_master_gain_bits
.type raw_audio_native_master_gain_bits,%function
raw_audio_native_master_gain_bits:
    /* Public ABI takes float bits in R0. The pinned native startup explicitly
       loads S0 before BL118080, and118080->111C30 consumes S0. */
    push {r4,lr}
    vmov s0,r0
    ldr r12,.Laudio_entry
    blx r12
    pop {r4,pc}
.Laudio_entry: .word 0x00118080
.size raw_audio_native_master_gain_bits,.-raw_audio_native_master_gain_bits
.section .note.GNU-stack,"",%progbits
