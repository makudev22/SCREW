#include <screw/bridge.h>
#include <screw/codegen.h>

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

void bridge_ms_to_sysv(void);
void bridge_sysv_to_ms(void);

__asm__(
    ".text\n"
    ".globl bridge_ms_to_sysv\n"
    ".type bridge_ms_to_sysv,@function\n"
    "bridge_ms_to_sysv:\n"
    "    push %rbp\n"
    "    mov %rsp, %rbp\n"
    "    push %rdi\n"
    "    push %rsi\n"
    "    sub $240, %rsp\n"
    "    movdqu %xmm6, 80(%rsp)\n"
    "    movdqu %xmm7, 96(%rsp)\n"
    "    movdqu %xmm8, 112(%rsp)\n"
    "    movdqu %xmm9, 128(%rsp)\n"
    "    movdqu %xmm10, 144(%rsp)\n"
    "    movdqu %xmm11, 160(%rsp)\n"
    "    movdqu %xmm12, 176(%rsp)\n"
    "    movdqu %xmm13, 192(%rsp)\n"
    "    movdqu %xmm14, 208(%rsp)\n"
    "    movdqu %xmm15, 224(%rsp)\n"
    "    mov 64(%rbp), %rax\n    mov %rax, 0(%rsp)\n"
    "    mov 72(%rbp), %rax\n    mov %rax, 8(%rsp)\n"
    "    mov 80(%rbp), %rax\n    mov %rax, 16(%rsp)\n"
    "    mov 88(%rbp), %rax\n    mov %rax, 24(%rsp)\n"
    "    mov 96(%rbp), %rax\n    mov %rax, 32(%rsp)\n"
    "    mov 104(%rbp), %rax\n    mov %rax, 40(%rsp)\n"
    "    mov 112(%rbp), %rax\n    mov %rax, 48(%rsp)\n"
    "    mov 120(%rbp), %rax\n    mov %rax, 56(%rsp)\n"
    "    mov 128(%rbp), %rax\n    mov %rax, 64(%rsp)\n"
    "    mov 136(%rbp), %rax\n    mov %rax, 72(%rsp)\n"
    "    mov 48(%rbp), %r10\n"
    "    mov 56(%rbp), %rax\n"
    "    mov %rcx, %rdi\n"
    "    mov %rdx, %rsi\n"
    "    mov %r8, %rdx\n"
    "    mov %r9, %rcx\n"
    "    mov %r10, %r8\n"
    "    mov %rax, %r9\n"
    "    xor %eax, %eax\n"
    "    call *%r11\n"
    "    movdqu 80(%rsp), %xmm6\n"
    "    movdqu 96(%rsp), %xmm7\n"
    "    movdqu 112(%rsp), %xmm8\n"
    "    movdqu 128(%rsp), %xmm9\n"
    "    movdqu 144(%rsp), %xmm10\n"
    "    movdqu 160(%rsp), %xmm11\n"
    "    movdqu 176(%rsp), %xmm12\n"
    "    movdqu 192(%rsp), %xmm13\n"
    "    movdqu 208(%rsp), %xmm14\n"
    "    movdqu 224(%rsp), %xmm15\n"
    "    add $240, %rsp\n"
    "    pop %rsi\n"
    "    pop %rdi\n"
    "    pop %rbp\n"
    "    ret\n"
    ".size bridge_ms_to_sysv, .-bridge_ms_to_sysv\n"

    ".globl bridge_sysv_to_ms\n"
    ".type bridge_sysv_to_ms,@function\n"
    "bridge_sysv_to_ms:\n"
    "    push %rbp\n"
    "    mov %rsp, %rbp\n"
    "    sub $128, %rsp\n"
    "    mov %r8, 32(%rsp)\n"
    "    mov %r9, 40(%rsp)\n"
    "    mov 16(%rbp), %rax\n    mov %rax, 48(%rsp)\n"
    "    mov 24(%rbp), %rax\n    mov %rax, 56(%rsp)\n"
    "    mov 32(%rbp), %rax\n    mov %rax, 64(%rsp)\n"
    "    mov 40(%rbp), %rax\n    mov %rax, 72(%rsp)\n"
    "    mov 48(%rbp), %rax\n    mov %rax, 80(%rsp)\n"
    "    mov 56(%rbp), %rax\n    mov %rax, 88(%rsp)\n"
    "    mov 64(%rbp), %rax\n    mov %rax, 96(%rsp)\n"
    "    mov 72(%rbp), %rax\n    mov %rax, 104(%rsp)\n"
    "    mov 80(%rbp), %rax\n    mov %rax, 112(%rsp)\n"
    "    mov 88(%rbp), %rax\n    mov %rax, 120(%rsp)\n"
    "    mov %rcx, %r9\n"
    "    mov %rdx, %r8\n"
    "    mov %rsi, %rdx\n"
    "    mov %rdi, %rcx\n"
    "    call *%r11\n"
    "    leave\n"
    "    ret\n"
    ".size bridge_sysv_to_ms, .-bridge_sysv_to_ms\n");

struct entry {
    void *from;
    void *stub;
    struct entry *next;
};

#define BUCKETS 1024

struct cache {
    struct entry *slots[BUCKETS];
    pthread_mutex_t lock;
};

static struct cache to_host = { .lock = PTHREAD_MUTEX_INITIALIZER };
static struct cache to_windows = { .lock = PTHREAD_MUTEX_INITIALIZER };

static void *cached(struct cache *c, void *fn, void *entry)
{
    if (!fn)
        return NULL;
    size_t b = ((uintptr_t)fn >> 4) % BUCKETS;
    pthread_mutex_lock(&c->lock);
    struct entry *e = c->slots[b];
    while (e && e->from != fn)
        e = e->next;
    if (!e) {
        e = malloc(sizeof *e);
        e->from = fn;
        e->stub = code_jump((uint64_t)fn, entry);
        e->next = c->slots[b];
        c->slots[b] = e;
    }
    pthread_mutex_unlock(&c->lock);
    return e->stub;
}

void *bridge_to_host(void *host_fn) { return cached(&to_host, host_fn, (void *)bridge_ms_to_sysv); }

void *bridge_to_windows(void *win_fn) { return cached(&to_windows, win_fn, (void *)bridge_sysv_to_ms); }
