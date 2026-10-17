#include <screw/teb.h>
#include <screw/log.h>

#include <asm/prctl.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

static struct peb peb;
static __thread struct teb *current;

struct peb *peb_get(void) { return &peb; }

struct teb *teb_current(void) { return current; }

struct teb *teb_create(void)
{
    struct teb *t = mmap(NULL, sizeof *t, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (t == MAP_FAILED)
        die("out of memory for thread block");
    t->self = t;
    t->peb = &peb;
    t->process_id = getpid();
    t->tls_pointer = t->implicit_tls;
    return t;
}

static void stack_bounds(struct teb *t)
{
    pthread_attr_t attr;
    void *addr;
    size_t size;
    if (pthread_getattr_np(pthread_self(), &attr))
        return;
    if (!pthread_attr_getstack(&attr, &addr, &size)) {
        t->stack_limit = addr;
        t->stack_base = (char *)addr + size;
    }
    pthread_attr_destroy(&attr);
}

void teb_bind(struct teb *t)
{
    t->thread_id = syscall(SYS_gettid);
    stack_bounds(t);
    if (syscall(SYS_arch_prctl, ARCH_SET_GS, (unsigned long)t))
        die("cannot install thread block in %%gs");
    current = t;
}

void teb_destroy(struct teb *t)
{
    if (current == t)
        current = NULL;
    free(t->tls_expansion);
    munmap(t, sizeof *t);
}
