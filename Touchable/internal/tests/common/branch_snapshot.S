// Test only. Capture CPU state before and after a registered indirect branch.
.text
.p2align 2
.globl _ss_test_snapshot
.globl _ss_test_trap
.globl _ss_test_target
_ss_test_snapshot:
    sub sp, sp, #256
    stp x19, x20, [sp, #0]
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    stp q8, q9, [sp, #96]
    stp q10, q11, [sp, #128]
    stp q12, q13, [sp, #160]
    stp q14, q15, [sp, #192]
    mrs x2, fpcr
    mrs x3, fpsr
    stp x2, x3, [sp, #224]
    stp x0, x1, [sp, #240]
    mov x29, sp
    mov x2, #0xa0000000
    msr nzcv, x2
    msr fpcr, xzr
    msr fpsr, xzr
    movi v0.16b, #1
    movi v1.16b, #2
    movi v2.16b, #3
    movi v3.16b, #4
    movi v4.16b, #5
    movi v5.16b, #6
    movi v6.16b, #7
    movi v7.16b, #8
    movi v8.16b, #9
    movi v9.16b, #10
    movi v10.16b, #11
    movi v11.16b, #12
    movi v12.16b, #13
    movi v13.16b, #14
    movi v14.16b, #15
    movi v15.16b, #16
    movi v16.16b, #17
    movi v17.16b, #18
    movi v18.16b, #19
    movi v19.16b, #20
    movi v20.16b, #21
    movi v21.16b, #22
    movi v22.16b, #23
    movi v23.16b, #24
    movi v24.16b, #25
    movi v25.16b, #26
    movi v26.16b, #27
    movi v27.16b, #28
    movi v28.16b, #29
    movi v29.16b, #30
    movi v30.16b, #31
    movi v31.16b, #32
    mov x0, #4096
    mov x1, #4097
    mov x2, #4098
    mov x3, #4099
    mov x4, #4100
    mov x5, #4101
    mov x6, #4102
    mov x7, #4103
    mov x8, #4104
    mov x9, #4105
    mov x10, #4106
    mov x11, #4107
    mov x12, #4108
    mov x13, #4109
    mov x14, #4110
    mov x15, #4111
    mov x16, #4112
    mov x17, #4113
    mov x19, #4115
    mov x20, #4116
    mov x21, #4117
    mov x22, #4118
    mov x23, #4119
    mov x24, #4120
    mov x25, #4121
    mov x26, #4122
    mov x27, #4123
    mov x28, #4124
    ldr x30, [sp, #240]
    stp x0, x1, [x30, #0]
    stp x2, x3, [x30, #16]
    stp x4, x5, [x30, #32]
    stp x6, x7, [x30, #48]
    stp x8, x9, [x30, #64]
    stp x10, x11, [x30, #80]
    stp x12, x13, [x30, #96]
    stp x14, x15, [x30, #112]
    stp x16, x17, [x30, #128]
    stp x18, x19, [x30, #144]
    stp x20, x21, [x30, #160]
    stp x22, x23, [x30, #176]
    stp x24, x25, [x30, #192]
    stp x26, x27, [x30, #208]
    stp x28, x29, [x30, #224]
    ldr x0, [sp, #88]
    str x0, [x30, #240]
    mov x0, sp
    str x0, [x30, #248]
    mrs x0, nzcv
    str x0, [x30, #256]
    mrs x0, fpcr
    str x0, [x30, #264]
    mrs x0, fpsr
    str x0, [x30, #272]
    stp q0, q1, [x30, #288]
    stp q2, q3, [x30, #320]
    stp q4, q5, [x30, #352]
    stp q6, q7, [x30, #384]
    stp q8, q9, [x30, #416]
    stp q10, q11, [x30, #448]
    stp q12, q13, [x30, #480]
    stp q14, q15, [x30, #512]
    stp q16, q17, [x30, #544]
    stp q18, q19, [x30, #576]
    stp q20, q21, [x30, #608]
    stp q22, q23, [x30, #640]
    stp q24, q25, [x30, #672]
    stp q26, q27, [x30, #704]
    stp q28, q29, [x30, #736]
    stp q30, q31, [x30, #768]
    ldr x0, [x30]
    ldr x30, [sp, #88]
_ss_test_trap:
    brk #0x6e18
    brk #0xdead
_ss_test_target:
    ldr x30, [sp, #248]
    stp x0, x1, [x30, #0]
    stp x2, x3, [x30, #16]
    stp x4, x5, [x30, #32]
    stp x6, x7, [x30, #48]
    stp x8, x9, [x30, #64]
    stp x10, x11, [x30, #80]
    stp x12, x13, [x30, #96]
    stp x14, x15, [x30, #112]
    stp x16, x17, [x30, #128]
    stp x18, x19, [x30, #144]
    stp x20, x21, [x30, #160]
    stp x22, x23, [x30, #176]
    stp x24, x25, [x30, #192]
    stp x26, x27, [x30, #208]
    stp x28, x29, [x30, #224]
    ldr x0, [sp, #88]
    str x0, [x30, #240]
    mov x0, sp
    str x0, [x30, #248]
    mrs x0, nzcv
    str x0, [x30, #256]
    mrs x0, fpcr
    str x0, [x30, #264]
    mrs x0, fpsr
    str x0, [x30, #272]
    stp q0, q1, [x30, #288]
    stp q2, q3, [x30, #320]
    stp q4, q5, [x30, #352]
    stp q6, q7, [x30, #384]
    stp q8, q9, [x30, #416]
    stp q10, q11, [x30, #448]
    stp q12, q13, [x30, #480]
    stp q14, q15, [x30, #512]
    stp q16, q17, [x30, #544]
    stp q18, q19, [x30, #576]
    stp q20, q21, [x30, #608]
    stp q22, q23, [x30, #640]
    stp q24, q25, [x30, #672]
    stp q26, q27, [x30, #704]
    stp q28, q29, [x30, #736]
    stp q30, q31, [x30, #768]
    ldr x0, [x30]
    ldr x30, [sp, #88]
    ldp x19, x20, [sp, #0]
    ldp x21, x22, [sp, #16]
    ldp x23, x24, [sp, #32]
    ldp x25, x26, [sp, #48]
    ldp x27, x28, [sp, #64]
    ldp x29, x30, [sp, #80]
    ldp q8, q9, [sp, #96]
    ldp q10, q11, [sp, #128]
    ldp q12, q13, [sp, #160]
    ldp q14, q15, [sp, #192]
    ldp x2, x3, [sp, #224]
    msr fpcr, x2
    msr fpsr, x3
    add sp, sp, #256
    ret

.p2align 2
.globl _ss_test_stack_pivot
.globl _ss_test_sp_trap
.globl _ss_test_sp_resume
_ss_test_stack_pivot:
    stp x19, x30, [sp, #-16]!
    mov x19, sp
_ss_test_sp_trap:
    brk #0x6e18
_ss_test_sp_resume:
    mov x0, sp
    mov sp, x19
    ldp x19, x30, [sp], #16
    ret

.p2align 2
.globl _ss_test_call
.globl _ss_test_call_trap
.globl _ss_test_call_resume
.globl _ss_test_call_target
_ss_test_call:
    stp x29, x30, [sp, #-16]!
_ss_test_call_trap:
    brk #0x6e18
_ss_test_call_resume:
    ldp x29, x30, [sp], #16
    ret
_ss_test_call_target:
    mov x0, x30
    ret

