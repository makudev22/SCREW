#include <screw/loader.h>
#include <screw/log.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define POOL_SIZE 65536
#define STUB_SIZE 24

static uint8_t *pool;
static size_t used = POOL_SIZE;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static _Noreturn void trap_fire(const char *what)
{
    die("missing %s (not implemented yet; SCREW_LENIENT=1 makes it return 0)", what);
}

static uint8_t *slot(void)
{
    if (used + STUB_SIZE > POOL_SIZE) {
        pool = mmap(NULL, POOL_SIZE, PROT_READ | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (pool == MAP_FAILED)
            die("out of memory for import stubs");
        used = 0;
    }
    uint8_t *s = pool + used;
    used += STUB_SIZE;
    return s;
}

static void emit(uint8_t *s, const char *what)
{
    uint8_t code[STUB_SIZE] = { 0x48, 0xBF };
    uint64_t arg = (uint64_t)what, fn = (uint64_t)trap_fire;
    memcpy(code + 2, &arg, 8);
    code[10] = 0x48;
    code[11] = 0xB8;
    memcpy(code + 12, &fn, 8);
    code[20] = 0xFF;
    code[21] = 0xE0;
    memcpy(s, code, sizeof code);
}

void *trap_make(const char *dll, const char *func)
{
    static const uint8_t zero_ret[] = { 0x31, 0xC0, 0xC3 };

    char *what = malloc(strlen(dll) + strlen(func) + 2);
    sprintf(what, "%s!%s", dll, func);
    if (log_lenient())
        warn("missing %s, returning 0", what);
    else
        trace("missing %s, trapping on call", what);

    pthread_mutex_lock(&lock);
    uint8_t *s = slot();
    mprotect(pool, POOL_SIZE, PROT_READ | PROT_WRITE);
    if (log_lenient())
        memcpy(s, zero_ret, sizeof zero_ret);
    else
        emit(s, what);
    mprotect(pool, POOL_SIZE, PROT_READ | PROT_EXEC);
    pthread_mutex_unlock(&lock);
    return s;
}
