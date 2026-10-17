#include <screw/bridge.h>
#include <screw/win.h>

#include <stdint.h>
#include <stdio.h>

typedef uint64_t (*sysv12)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                           uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
typedef uint64_t WINAPI (*ms12)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

static uint64_t host_sum(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f,
                         uint64_t g, uint64_t h, uint64_t i, uint64_t j, uint64_t k, uint64_t l)
{
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + 9 * i + 10 * j + 11 * k + 12 * l;
}

static uint64_t WINAPI win_sum(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f,
                               uint64_t g, uint64_t h, uint64_t i, uint64_t j, uint64_t k, uint64_t l)
{
    return host_sum(a, b, c, d, e, f, g, h, i, j, k, l);
}

static double host_half(double x) { return x / 2; }

static int clobber_check(void)
{
    ms12 to_host = (ms12)bridge_to_host((void *)host_sum);
    uint64_t rdi_before = 0x1111, rsi_before = 0x2222, rdi_after, rsi_after;
    double keep = 6.5, keep_after;
    uint64_t r;
    __asm__ volatile("movsd %[k], %%xmm6\n"
                     "mov %[a], %%rdi\n"
                     "mov %[b], %%rsi\n"
                     : : [k] "m"(keep), [a] "r"(rdi_before), [b] "r"(rsi_before) : "rdi", "rsi", "xmm6");
    r = to_host(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
    __asm__ volatile("mov %%rdi, %[a]\n"
                     "mov %%rsi, %[b]\n"
                     "movsd %%xmm6, %[k]\n"
                     : [a] "=r"(rdi_after), [b] "=r"(rsi_after), [k] "=m"(keep_after));
    (void)r;
    return rdi_after == rdi_before && rsi_after == rsi_before && keep_after == keep;
}

int main(void)
{
    uint64_t want = host_sum(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
    ms12 to_host = (ms12)bridge_to_host((void *)host_sum);
    sysv12 to_win = (sysv12)bridge_to_windows((void *)win_sum);
    uint64_t a = to_host(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
    uint64_t b = to_win(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
    double WINAPI (*half)(double) = (double WINAPI (*)(double))bridge_to_host((void *)host_half);
    int fails = 0;
    fails += a != want;
    fails += b != want;
    fails += half(9.0) != 4.5;
    fails += bridge_to_host((void *)host_sum) != (void *)to_host;
    fails += !clobber_check();
    printf("bridge: ms->sysv %s, sysv->ms %s, float %s, cache %s, callee-saved %s\n",
           a == want ? "ok" : "BAD", b == want ? "ok" : "BAD", half(9.0) == 4.5 ? "ok" : "BAD",
           bridge_to_host((void *)host_sum) == (void *)to_host ? "ok" : "BAD", clobber_check() ? "ok" : "BAD");
    return fails != 0;
}
