#include <screw/codegen.h>
#include <screw/log.h>

#include <pthread.h>
#include <string.h>
#include <sys/mman.h>

#define POOL_SIZE (256 * 1024)
#define ALIGN 16

static uint8_t *pool;
static size_t used = POOL_SIZE;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void *code_emit(const void *bytes, size_t n)
{
    size_t size = (n + ALIGN - 1) & ~(size_t)(ALIGN - 1);
    pthread_mutex_lock(&lock);
    if (used + size > POOL_SIZE) {
        pool = mmap(NULL, POOL_SIZE, PROT_READ | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (pool == MAP_FAILED)
            die("out of memory for generated code");
        used = 0;
    }
    uint8_t *at = pool + used;
    used += size;
    uintptr_t page = (uintptr_t)at & ~(uintptr_t)4095;
    size_t span = ((uintptr_t)at + n - page + 4095) & ~(size_t)4095;
    mprotect((void *)page, span, PROT_READ | PROT_WRITE);
    memcpy(at, bytes, n);
    mprotect((void *)page, span, PROT_READ | PROT_EXEC);
    pthread_mutex_unlock(&lock);
    return at;
}

static void *load_and_jump(uint8_t reg_prefix, uint8_t reg_op, uint64_t value, void *target)
{
    uint8_t code[22] = { reg_prefix, reg_op };
    uint64_t t = (uint64_t)target;
    memcpy(code + 2, &value, 8);
    code[10] = 0x48;
    code[11] = 0xB8;
    memcpy(code + 12, &t, 8);
    code[20] = 0xFF;
    code[21] = 0xE0;
    return code_emit(code, sizeof code);
}

void *code_jump(uint64_t r11_value, void *target) { return load_and_jump(0x49, 0xBB, r11_value, target); }

void *code_call1(uint64_t rdi_value, void *target) { return load_and_jump(0x48, 0xBF, rdi_value, target); }
