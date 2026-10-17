#include <screw/codegen.h>
#include <screw/log.h>

#include <pthread.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define POOL_SIZE (256 * 1024)
#define ALIGN 16

static uint8_t *exec_view;
static uint8_t *write_view;
static size_t used = POOL_SIZE;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static void new_pool(void)
{
    int fd = memfd_create("screw-code", MFD_CLOEXEC);
    if (fd >= 0 && !ftruncate(fd, POOL_SIZE)) {
        write_view = mmap(NULL, POOL_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        exec_view = mmap(NULL, POOL_SIZE, PROT_READ | PROT_EXEC, MAP_SHARED, fd, 0);
    }
    if (fd >= 0)
        close(fd);
    if (fd < 0 || write_view == MAP_FAILED || exec_view == MAP_FAILED)
        die("out of memory for generated code");
    used = 0;
}

void *code_emit(const void *bytes, size_t n)
{
    size_t size = (n + ALIGN - 1) & ~(size_t)(ALIGN - 1);
    pthread_mutex_lock(&lock);
    if (used + size > POOL_SIZE)
        new_pool();
    memcpy(write_view + used, bytes, n);
    uint8_t *at = exec_view + used;
    used += size;
    pthread_mutex_unlock(&lock);
    __builtin___clear_cache((char *)at, (char *)at + n);
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

void *code_null_arg(int index, void *target)
{
    static const uint8_t clear_reg[4][3] = {
        { 0x31, 0xC9 }, { 0x31, 0xD2 }, { 0x45, 0x31, 0xC0 }, { 0x45, 0x31, 0xC9 },
    };
    static const uint8_t clear_len[4] = { 2, 2, 3, 3 };
    uint8_t code[32];
    size_t n = 0;
    if (index < 4) {
        memcpy(code, clear_reg[index], clear_len[index]);
        n = clear_len[index];
    } else {
        int32_t disp = 8 + 8 * index;
        uint8_t store[] = { 0x48, 0xC7, 0x84, 0x24 };
        memcpy(code, store, sizeof store);
        memcpy(code + 4, &disp, 4);
        memset(code + 8, 0, 4);
        n = 12;
    }
    uint64_t t = (uint64_t)target;
    code[n++] = 0x48;
    code[n++] = 0xB8;
    memcpy(code + n, &t, 8);
    n += 8;
    code[n++] = 0xFF;
    code[n++] = 0xE0;
    return code_emit(code, n);
}
