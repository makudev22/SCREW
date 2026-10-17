#include <screw/crash.h>
#include <screw/log.h>
#include <screw/module.h>
#include <screw/teb.h>

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

#define ALT_STACK (64 * 1024)
#define STACK_SCAN 512
#define FRAMES_MAX 16

static const int fatal[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE };

static void out(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof buf - 1)
        n = sizeof buf - 1;
    int fd = log_fd();
    if (write(fd, buf, n) < 0)
        return;
    if (fd != 2 && write(2, buf, n) < 0)
        return;
}

static const char *signal_name(int sig)
{
    switch (sig) {
    case SIGSEGV: return "access violation (SIGSEGV)";
    case SIGBUS: return "bus error (SIGBUS)";
    case SIGILL: return "illegal instruction (SIGILL)";
    case SIGFPE: return "arithmetic error (SIGFPE)";
    default: return "fatal signal";
    }
}

static void where(const char *label, uint64_t addr)
{
    struct module *m = module_at((void *)addr);
    if (!m)
        out("  %-6s %016lx\n", label, (unsigned long)addr);
    else if (m->bit)
        out("  %-6s %016lx  in built-in %s\n", label, (unsigned long)addr, m->name);
    else
        out("  %-6s %016lx  %s+0x%lx\n", label, (unsigned long)addr, m->name,
            (unsigned long)(addr - (uint64_t)m->base));
}

static bool code_address(uint64_t v)
{
    struct module *m = module_at((void *)v);
    if (!m || m->bit || !m->nt)
        return false;
    uint64_t rva = v - (uint64_t)m->base;
    struct pe_section *sec = pe_sections(m->nt);
    for (unsigned i = 0; i < m->nt->file.section_count; i++)
        if ((sec[i].characteristics & PE_SCN_EXECUTE) && rva >= sec[i].rva &&
            rva < sec[i].rva + (sec[i].virtual_size ? sec[i].virtual_size : sec[i].raw_size))
            return true;
    return false;
}

static void handler(int sig, siginfo_t *info, void *ctx)
{
    ucontext_t *uc = ctx;
    greg_t *r = uc->uc_mcontext.gregs;
    uint64_t rip = r[REG_RIP], rsp = r[REG_RSP];

    out("\nscrew: the program crashed: %s\n", signal_name(sig));
    out("  thread %ld\n", (long)syscall(SYS_gettid));
    where("at", rip);
    if (sig == SIGSEGV || sig == SIGBUS)
        out("  %-6s %016lx  (%s)\n", "memory", (unsigned long)info->si_addr,
            info->si_addr ? "address that could not be accessed" : "null pointer");

    out("  registers:\n");
    out("    rax %016llx rbx %016llx rcx %016llx rdx %016llx\n", r[REG_RAX], r[REG_RBX], r[REG_RCX], r[REG_RDX]);
    out("    rsi %016llx rdi %016llx rbp %016llx rsp %016llx\n", r[REG_RSI], r[REG_RDI], r[REG_RBP], r[REG_RSP]);
    out("    r8  %016llx r9  %016llx r10 %016llx r11 %016llx\n", r[REG_R8], r[REG_R9], r[REG_R10], r[REG_R11]);
    out("    r12 %016llx r13 %016llx r14 %016llx r15 %016llx\n", r[REG_R12], r[REG_R13], r[REG_R14], r[REG_R15]);

    out("  likely callers:\n");
    uint64_t *stack = (uint64_t *)rsp;
    struct teb *t = teb_current();
    int limit = STACK_SCAN;
    if (t && t->stack_base && (uint64_t)t->stack_base > rsp && ((uint64_t)t->stack_base - rsp) / 8 < STACK_SCAN)
        limit = ((uint64_t)t->stack_base - rsp) / 8;
    int frames = 0;
    for (int i = 0; i < limit && frames < FRAMES_MAX; i++) {
        uint64_t v = stack[i];
        if (code_address(v)) {
            where("from", v);
            frames++;
        }
    }
    if (!frames)
        out("    none found on the stack\n");

    out("  modules:\n");
    for (struct module *m = module_list(); m; m = m->next)
        out("    %016lx %-24s %s\n", (unsigned long)m->base, m->name, m->bit ? "built-in" : m->path);
    out("  rerun with SCREW_TRACE=1 or SCREW_LOG=screw.log for the full load and call trace\n");

    signal(sig, SIG_DFL);
    raise(sig);
}

void crash_install(void)
{
    stack_t ss = { .ss_sp = malloc(ALT_STACK), .ss_size = ALT_STACK };
    if (ss.ss_sp)
        sigaltstack(&ss, NULL);
    struct sigaction sa = { 0 };
    sa.sa_sigaction = handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (size_t i = 0; i < sizeof fatal / sizeof fatal[0]; i++)
        sigaction(fatal[i], &sa, NULL);
}
