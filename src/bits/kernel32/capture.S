    .text
    .globl RtlCaptureContext
    .type RtlCaptureContext,@function
RtlCaptureContext:
    movq %rax, 0x78(%rcx)
    movq %rcx, 0x80(%rcx)
    movq %rdx, 0x88(%rcx)
    movq %rbx, 0x90(%rcx)
    movq %rbp, 0xA0(%rcx)
    movq %rsi, 0xA8(%rcx)
    movq %rdi, 0xB0(%rcx)
    movq %r8, 0xB8(%rcx)
    movq %r9, 0xC0(%rcx)
    movq %r10, 0xC8(%rcx)
    movq %r11, 0xD0(%rcx)
    movq %r12, 0xD8(%rcx)
    movq %r13, 0xE0(%rcx)
    movq %r14, 0xE8(%rcx)
    movq %r15, 0xF0(%rcx)
    leaq 8(%rsp), %rax
    movq %rax, 0x98(%rcx)
    movq (%rsp), %rax
    movq %rax, 0xF8(%rcx)
    movq %rax, 0x78(%rcx)
    movl $0x0010000b, 0x30(%rcx)
    stmxcsr 0x34(%rcx)
    movdqa %xmm0, 0x1A0(%rcx)
    movdqa %xmm1, 0x1B0(%rcx)
    movdqa %xmm2, 0x1C0(%rcx)
    movdqa %xmm3, 0x1D0(%rcx)
    movdqa %xmm4, 0x1E0(%rcx)
    movdqa %xmm5, 0x1F0(%rcx)
    movdqa %xmm6, 0x200(%rcx)
    movdqa %xmm7, 0x210(%rcx)
    movdqa %xmm8, 0x220(%rcx)
    movdqa %xmm9, 0x230(%rcx)
    movdqa %xmm10, 0x240(%rcx)
    movdqa %xmm11, 0x250(%rcx)
    movdqa %xmm12, 0x260(%rcx)
    movdqa %xmm13, 0x270(%rcx)
    movdqa %xmm14, 0x280(%rcx)
    movdqa %xmm15, 0x290(%rcx)
    ret
    .size RtlCaptureContext, .-RtlCaptureContext

    .globl screw_restore_context
    .type screw_restore_context,@function
screw_restore_context:
    movl 0x34(%rcx), %eax
    ldmxcsr 0x34(%rcx)
    movdqa 0x1A0(%rcx), %xmm0
    movdqa 0x1B0(%rcx), %xmm1
    movdqa 0x1C0(%rcx), %xmm2
    movdqa 0x1D0(%rcx), %xmm3
    movdqa 0x1E0(%rcx), %xmm4
    movdqa 0x1F0(%rcx), %xmm5
    movdqa 0x200(%rcx), %xmm6
    movdqa 0x210(%rcx), %xmm7
    movdqa 0x220(%rcx), %xmm8
    movdqa 0x230(%rcx), %xmm9
    movdqa 0x240(%rcx), %xmm10
    movdqa 0x250(%rcx), %xmm11
    movdqa 0x260(%rcx), %xmm12
    movdqa 0x270(%rcx), %xmm13
    movdqa 0x280(%rcx), %xmm14
    movdqa 0x290(%rcx), %xmm15
    movq 0x78(%rcx), %rax
    movq 0x88(%rcx), %rdx
    movq 0x90(%rcx), %rbx
    movq 0xA0(%rcx), %rbp
    movq 0xA8(%rcx), %rsi
    movq 0xB0(%rcx), %rdi
    movq 0xB8(%rcx), %r8
    movq 0xC0(%rcx), %r9
    movq 0xC8(%rcx), %r10
    movq 0xD0(%rcx), %r11
    movq 0xD8(%rcx), %r12
    movq 0xE0(%rcx), %r13
    movq 0xE8(%rcx), %r14
    movq 0xF0(%rcx), %r15
    movq 0x98(%rcx), %rsp
    pushq 0xF8(%rcx)
    movq 0x80(%rcx), %rcx
    ret
    .size screw_restore_context, .-screw_restore_context

    .section .note.GNU-stack,"",@progbits
