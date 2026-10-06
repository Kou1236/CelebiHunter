@ Altered subset of CTRPluginFramework Library/source/csvc.s for this
@ research loader: only the five csVC imports used by the manual backend.
@ Original license and implementation below are retained.
@
@ This software is provided 'as-is', without any express or implied warranty. In no event will the authors be held liable
@ for any damages arising from the use of this software.
@
@ Permission is granted to anyone to use this software for any purpose, including commercial applications, and to alter it
@ and redistribute it freely, subject to the following restrictions:
@
@ The origin of this software must not be misrepresented; you must not claim that you wrote the original software.
@ If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
@
@ Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
@ This notice may not be removed or altered from any source distribution.
.syntax unified
.cpu mpcore
.arm
.macro SVC_BEGIN name
    .section .text.\name,"ax",%progbits
    .global \name
    .type \name,%function
    .balign 4
\name:
.endm
.macro SVC_END name
    .size \name,.-\name
.endm
SVC_BEGIN svcConvertVAToPA
    svc 0x90
    bx lr
SVC_END svcConvertVAToPA
SVC_BEGIN svcFlushEntireDataCache
    svc 0x92
    bx lr
SVC_END svcFlushEntireDataCache
SVC_BEGIN svcInvalidateEntireInstructionCache
    svc 0x94
    bx lr
SVC_END svcInvalidateEntireInstructionCache
SVC_BEGIN svcMapProcessMemoryEx
    push {r4,r5,r6}
    ldr r4,[sp,#12]
    ldr r5,[sp,#16]
    mov r6,r0
    mov r0,#0xFFFFFFF2
    svc 0xA0
    pop {r4,r5,r6}
    bx lr
SVC_END svcMapProcessMemoryEx
SVC_BEGIN svcUnmapProcessMemoryEx
    svc 0xA1
    bx lr
SVC_END svcUnmapProcessMemoryEx
.section .note.GNU-stack,"",%progbits
